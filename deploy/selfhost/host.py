"""Host provisioning shared by the installer and installation-local CLI.

No secrets are stored in the host registry. All subprocesses use argument lists.
"""
import contextlib
import json
import math
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys

REGISTRY = Path('/var/lib/nimbo-installer')
PORT_FIELDS = ('api_port', 'signal_port', 'rtc_tcp_port', 'turn_tls_port', 'rtc_udp_port', 'turn_udp_port')


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def free_port(number, udp=False):
    if not 1024 <= number <= 65535 and number != 443:
        return False
    # Check both address families. IPv4-only probes miss IPv6-only listeners.
    for family, address in ((socket.AF_INET, '0.0.0.0'), (socket.AF_INET6, '::')):
        try:
            with socket.socket(family, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM) as probe:
                if family == socket.AF_INET6:
                    probe.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
                probe.bind((address, number))
        except OSError as error:
            if family == socket.AF_INET6 and error.errno in (97, 99):
                continue
            return False
    return True


def select_port(value, records, interactive=None, ask=input):
    reserved = {p for record in records.values() for p in record['tcp']}
    if value is None:
        if interactive is None:
            interactive = sys.stdin.isatty()
        if not interactive:
            raise ValueError('Укажите --https-port: без терминала порт нужно выбрать явно.')
        suggestion = next((n for n in (7443, 9443, 8443, *range(10443, 65536))
                           if n not in reserved and free_port(n)), None)
        if suggestion is None:
            raise ValueError('Не найден свободный порт подключения.')
        while True:
            try:
                raw = ask(f'Порт подключения Nimbo [{suggestion}]: ').strip()
                value = int(raw) if raw else suggestion
            except (ValueError, EOFError):
                raise ValueError('Порт не выбран; используйте --https-port.') from None
            if value not in reserved and free_port(value):
                break
            print('Порт занят, зарезервирован или недопустим. Выберите другой.')
    if value in reserved or not free_port(value):
        raise ValueError(f'Порт {value} занят, зарезервирован или недопустим (443 либо 1024–65535).')
    return value


def allocate_ports(a, records):
    used = {kind: {p for r in records.values() for p in r[kind]} for kind in ('tcp', 'udp')}
    used['tcp'].add(a.https_port)
    # Honour all explicitly selected ports before allocating any defaults.
    for field in PORT_FIELDS:
        value = getattr(a, field)
        kind = 'udp' if 'udp' in field else 'tcp'
        if value is not None:
            if value in used[kind] or not free_port(value, kind == 'udp'):
                raise ValueError(f'{field}: порт {value}/{kind} недоступен.')
            used[kind].add(value)
    for field in PORT_FIELDS:
        if getattr(a, field) is not None:
            continue
        kind = 'udp' if 'udp' in field else 'tcp'
        value = next((n for n in range(20000, 40000)
                      if n not in used[kind] and free_port(n, kind == 'udp')), None)
        if value is None:
            raise ValueError(f'Нет свободных служебных портов {kind}.')
        used[kind].add(value)
        setattr(a, field, value)


@contextlib.contextmanager
def registry_lock():
    import fcntl
    REGISTRY.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (REGISTRY / 'install.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        path = REGISTRY / 'instances.json'
        records = json.loads(path.read_text()) if path.exists() else {}
        yield records


def reserve(a, records):
    records[a.project] = {
        'directory': str(a.directory), 'tcp': [a.https_port, a.api_port, a.signal_port, a.rtc_tcp_port, a.turn_tls_port],
        'udp': [a.rtc_udp_port, a.turn_udp_port], 'memoryMiB': a.memory_mib + (3072 if a.antivirus_enabled else 0),
        'cpus': a.cpus, 'storageGiB': a.storage_gib,
    }
    temporary = REGISTRY / 'instances.json.tmp'
    temporary.write_text(json.dumps(records, indent=2) + '\n')
    temporary.chmod(0o600)
    temporary.replace(REGISTRY / 'instances.json')


def resource_preflight(a, records, ram):
    if a.project in records:
        raise ValueError('Имя установки уже зарезервировано, даже если контейнеры остановлены.')
    if any(Path(r['directory']).resolve() == a.directory.resolve() for r in records.values()):
        raise ValueError('Каталог уже зарезервирован другой установкой.')
    if a.memory_mib < 768 or not math.isfinite(a.cpus) or a.cpus < 0.5 or a.storage_gib < 2:
        raise ValueError('Минимум: --memory-mib 768, --cpus 0.5, --storage-gib 2.')
    memory = a.memory_mib + (3072 if a.antivirus_enabled else 0)
    claimed = sum(r['memoryMiB'] for r in records.values())
    if ram is None or (memory + claimed + 512) * 1024**2 > ram:
        raise ValueError('Недостаточно ОЗУ под лимиты установок и запас 512 MiB для ОС. Уменьшите лимит, выберите --antivirus off или увеличьте RAM.')
    if a.cpus + sum(r['cpus'] for r in records.values()) > (os.cpu_count() or 1):
        raise ValueError('Сумма --cpus превышает число CPU хоста. Выберите меньший бюджет.')
    parent = a.directory.parent
    while not parent.exists():
        parent = parent.parent
    if shutil.disk_usage(parent).free < (a.storage_gib + 3) * 1024**3:
        raise ValueError('На диске не хватает места под отдельный том и запас 3 GiB для сборки/ОС.')
    print(f'Лимиты: {memory} MiB RAM, {a.cpus:g} CPU, отдельный том {a.storage_gib} GiB.')
    print('Учтены установки нового установщика; сторонние службы требуют дополнительного запаса.')


def bind(source, target, readonly=False):
    return {'type': 'bind', 'source': source, 'target': target, 'read_only': readonly,
            'bind': {'create_host_path': False}}


def prepare_storage(root, gib):
    """Preallocate a bounded filesystem; no sparse overcommit and no /etc/fstab edits."""
    image = root / 'storage.ext4'
    if image.exists() or (root / 'storage').exists():
        raise ValueError('Storage already exists; refusing to format or replace it.')
    mount = root / 'storage'
    mount.mkdir(mode=0o700)
    run('fallocate', '-l', str(gib * 1024**3), str(image))
    image.chmod(0o600)
    run('mkfs.ext4', '-q', '-m', '1', str(image))
    unit = run('systemd-escape', '--path', '--suffix=mount', str(mount), capture_output=True, text=True).stdout.strip()
    unit_path = Path('/etc/systemd/system') / unit
    with unit_path.open('x') as f:
        f.write(f'[Unit]\nDescription=Nimbo bounded storage\nBefore=docker.service\n\n[Mount]\nWhat={image}\nWhere={mount}\nType=ext4\nOptions=loop,nodev,nosuid,noexec,noatime\n\n[Install]\nWantedBy=local-fs.target\n')
    run('systemctl', 'daemon-reload')
    run('systemctl', 'enable', '--now', unit)
    for name in ('data', 'antivirus-db'):
        (mount / name).mkdir(mode=0o700)
        os.chown(mount / name, 65532, 65532)
    (root / 'data').symlink_to(mount / 'data', target_is_directory=True)
    return unit


def certificate_command(settings, root, renew=False):
    command = ['certbot', 'renew' if renew else 'certonly', '--non-interactive', '--cert-name', settings['project'],
               '--config-dir', str(root / 'private/acme'), '--work-dir', str(root / 'private/acme-work'),
               '--logs-dir', str(root / 'private/acme-logs')]
    if not renew:
        command += ['--agree-tos', '--register-unsafely-without-email']
        if settings.get('acmeWebroot'):
            command += ['--webroot', '-w', settings['acmeWebroot']]
        else:
            command += ['--standalone', '--preferred-challenges', 'http']
        for name in settings['domains']:
            command += ['-d', name]
    return command


def certificates(root, settings, renew=False):
    """Isolated ACME account and renewal config. Never reads another installation's keys."""
    import fcntl
    REGISTRY.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (REGISTRY / 'acme.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        run(*certificate_command(settings, root, renew))
    source = root / 'private/acme/live' / settings['project']
    dest = root / 'private/certificates'
    dest.mkdir(mode=0o700, exist_ok=True)
    crt, key = (source / 'fullchain.pem').read_bytes(), (source / 'privkey.pem').read_bytes()
    changed = not (dest / 'app.crt').exists() or (dest / 'app.crt').read_bytes() != crt
    run('openssl', 'x509', '-in', str(source / 'fullchain.pem'), '-checkend', '86400', '-noout', stdout=subprocess.DEVNULL)
    for domain in settings['domains']:
        run('openssl', 'x509', '-in', str(source / 'fullchain.pem'), '-checkhost', domain, '-noout', stdout=subprocess.DEVNULL)
    pub = run('openssl', 'x509', '-in', str(source / 'fullchain.pem'), '-pubkey', '-noout', capture_output=True).stdout
    if pub != run('openssl', 'pkey', '-in', str(source / 'privkey.pem'), '-pubout', capture_output=True).stdout:
        raise ValueError('ACME certificate/key mismatch.')
    if changed:
        for name in ('app', 'rtc', 'turn'):
            for ext, body in (('crt', crt), ('key', key)):
                temp = dest / f'{name}.{ext}.tmp'
                temp.write_bytes(body)
                temp.chmod(0o600)
                temp.replace(dest / f'{name}.{ext}')
    return changed


def renewal_timer(root, project):
    base = Path('/etc/systemd/system') / f'nimbo-{project}-certificates'
    for suffix, body in (
        ('.service', f'[Unit]\nDescription=Nimbo certificate renewal ({project})\nAfter=network-online.target docker.service\n\n[Service]\nType=oneshot\nExecStart={root}/nimbo renew-certificates\n'),
        ('.timer', '[Unit]\nDescription=Nimbo certificate renewal schedule\n\n[Timer]\nOnCalendar=*-*-* 03,15:00:00\nRandomizedDelaySec=3600\nPersistent=true\n\n[Install]\nWantedBy=timers.target\n')):
        path = Path(str(base) + suffix)
        if path.exists():
            if path.read_text() != body:
                raise ValueError(f'Existing renewal unit differs: {path}')
        else:
            with path.open('x') as f:
                f.write(body)
    run('systemctl', 'daemon-reload')
    run('systemctl', 'enable', '--now', base.name + '.timer')


def public_ports(a):
    return {'tcp': [a.https_port, a.rtc_tcp_port, a.turn_tls_port], 'udp': [a.rtc_udp_port, a.turn_udp_port]}


def firewall(a):
    # Do not enable/reset a firewall or mix different firewall managers.
    if a.firewall == 'manual':
        print('Firewall: откройте только публичные порты: ' + json.dumps(public_ports(a)))
        return
    if not shutil.which('ufw') or 'Status: active' not in run('ufw', 'status', capture_output=True, text=True, env={**os.environ, 'LC_ALL': 'C'}).stdout:
        raise ValueError('--firewall ufw требует уже включённый UFW; SSH и правила других служб не меняются.')
    ports = public_ports(a)
    if not a.certificate_directory:
        ports['tcp'].append(80)
    for transport, values in ports.items():
        for number in values:
            run('ufw', 'allow', f'{number}/{transport}', 'comment', f'Nimbo {a.project}')
