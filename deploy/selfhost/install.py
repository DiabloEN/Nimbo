#!/usr/bin/env python3
"""Install an independent Nimbo stack from a locally unpacked release bundle."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import socket
import subprocess
import sys

LIVEKIT = 'livekit/livekit-server@sha256:89ca9ce0791e18d6e03bf6e2c49ceb27e1c754eae8bd50c989386c1a5963791a'
CADDY = 'livekit/caddyl4@sha256:9dd090159a6042a907dbff0ce9dda27a21d2a559dc7b03b60a8e6b1b60b8348e'


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def domain(value):
    value = value.lower()
    if not re.fullmatch(r'(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?', value):
        raise argparse.ArgumentTypeError('Use a DNS hostname without a scheme, port or path')
    return value


def port(value):
    n = int(value)
    if not 1024 <= n <= 65535:
        raise argparse.ArgumentTypeError('Internal/media ports must be between 1024 and 65535')
    return n


def arguments():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory', type=Path, default=Path('/opt/nimbo-selfhost'))
    p.add_argument('--project', default='nimbo-selfhost')
    p.add_argument('--domain', type=domain, required=True)
    p.add_argument('--rtc-domain', type=domain, required=True)
    p.add_argument('--turn-domain', type=domain, required=True)
    p.add_argument('--public-ip', type=ipaddress.IPv4Address, required=True)
    p.add_argument('--https-port', type=int, default=443)
    p.add_argument('--api-port', type=port, default=8090)
    p.add_argument('--signal-port', type=port, default=7880)
    p.add_argument('--rtc-tcp-port', type=port, default=7881)
    p.add_argument('--rtc-udp-port', type=port, default=7882)
    p.add_argument('--turn-tls-port', type=port, default=5349)
    p.add_argument('--turn-udp-port', type=port, default=3478)
    p.add_argument('--certificate-directory', type=Path,
                   help='Existing PEM pairs app.crt/app.key, rtc.crt/rtc.key, turn.crt/turn.key; copied, never modified')
    p.add_argument('--web-preview', action='store_true', help='Expose the development web client for acceptance testing')
    p.add_argument('--prepare-only', action='store_true', help='Write configuration, but do not build, initialize or start containers')
    p.add_argument('--skip-dns-check', action='store_true', help='Only for preconfigured NAT/DNS environments')
    return p.parse_args()


def configuration(a):
    suffix = '' if a.https_port == 443 else f':{a.https_port}'
    origin = f'https://{a.domain}{suffix}'
    key, secret = 'nimbo_' + secrets.token_urlsafe(12), secrets.token_urlsafe(48)
    env = '\n'.join([
        f'NIMBO_LISTEN=127.0.0.1:{a.api_port}', f'NIMBO_ORIGIN={origin}',
        f'NIMBO_SESSION_NAMESPACE={a.project}',
        f'LIVEKIT_URL=wss://{a.rtc_domain}{suffix}', f'LIVEKIT_API_KEY={key}',
        f'LIVEKIT_API_SECRET={secret}', f'NIMBO_TEST_PASSWORD={secrets.token_urlsafe(32)}',
        'NIMBO_DATA_FILE=/var/lib/nimbo/workspace.json',
        f'LIVEKIT_API_URL=http://127.0.0.1:{a.signal_port}', ''])
    # JSON is also valid YAML; keep untrusted input out of string templates.
    livekit = {'port': a.signal_port, 'bind_addresses': ['127.0.0.1'],
        'rtc': {'tcp_port': a.rtc_tcp_port, 'udp_port': a.rtc_udp_port,
                'use_external_ip': False, 'ips': {'includes': [f'{a.public_ip}/32']}},
        'turn': {'enabled': True, 'domain': a.turn_domain, 'tls_port': a.turn_tls_port,
                 'udp_port': a.turn_udp_port, 'external_tls': True,
                 'bind_addresses': ['0.0.0.0'], 'proxy_protocol': True,
                 'proxy_protocol_trusted_cidrs': ['127.0.0.0/8', '::1/128']},
        'keys': {key: secret}, 'room': {'max_participants': 10}, 'logging': {'level': 'warn'}}

    def proxy(host, port, paths=None):
        match = {'host': [host]}
        if paths:
            match['path'] = paths
        return {'match': [match], 'handle': [{'handler': 'reverse_proxy',
            'upstreams': [{'dial': f'127.0.0.1:{port}'}],
            'headers': {'request': {'set': {'X-Real-IP': ['{http.request.remote.host}']}}}}], 'terminal': True}

    http = {'listen': [f':{a.https_port}'], 'protocols': ['h1', 'h2'],
        'listener_wrappers': [{'wrapper': 'layer4', 'routes': [{
            'match': [{'tls': {'sni': [a.turn_domain]}}],
            'handle': [{'handler': 'tls'}, {'handler': 'proxy', 'proxy_protocol': 'v2',
                       'upstreams': [{'dial': [f'127.0.0.1:{a.turn_tls_port}']}]}]}]}, {'wrapper': 'tls'}],
        'tls_connection_policies': [{}],
        'routes': [proxy(a.domain, a.api_port, None if a.web_preview else ['/api/*', '/healthz']),
                   proxy(a.rtc_domain, a.signal_port),
                   {'handle': [{'handler': 'static_response', 'status_code': 404}]}]}
    if a.certificate_directory:
        http['automatic_https'] = {'disable': True}
        # With a custom public port, terminate TURN TLS in LiveKit itself.
        # external_tls advertises 443 and is only appropriate for the shared-443 proxy.
        http.pop('listener_wrappers')
        livekit['turn'].update({'external_tls': False, 'proxy_protocol': False,
                               'cert_file': '/certificates/turn.crt', 'key_file': '/certificates/turn.key'})
        certificates = {'load_files': [{'certificate': f'/certificates/{name}.crt',
            'key': f'/certificates/{name}.key'} for name in ('app', 'rtc', 'turn')]}
    else:
        certificates = {'automate': [a.domain, a.rtc_domain, a.turn_domain]}
    caddy = {'admin': {'disabled': True}, 'apps': {'tls': {'certificates': certificates},
             'http': {'servers': {'nimbo': http}}}}
    common = {'network_mode': 'host', 'restart': 'unless-stopped', 'read_only': True,
              'cap_drop': ['ALL'], 'security_opt': ['no-new-privileges:true'],
              'logging': {'driver': 'json-file', 'options': {'max-size': '10m', 'max-file': '3'}}}
    compose = {'name': a.project, 'services': {
        'nimbo': {**common, 'build': {'context': '.', 'dockerfile': 'Dockerfile.binary'},
            'image': f'{a.project}-api:local', 'env_file': ['./private/nimbo.env'],
            'volumes': ['./data:/var/lib/nimbo'], 'pids_limit': 128, 'mem_limit': '256m', 'stop_grace_period': '20s'},
        'livekit': {**common, 'image': LIVEKIT, 'command': ['--config', '/etc/livekit.yaml'],
            'volumes': ['./private/livekit.yaml:/etc/livekit.yaml:ro'] +
                       (['./private/certificates:/certificates:ro'] if a.certificate_directory else []),
            'tmpfs': ['/tmp:size=64m,mode=1777'], 'pids_limit': 512, 'mem_limit': '1g', 'stop_grace_period': '30s'},
        'caddy': {**common, 'image': CADDY, 'entrypoint': ['caddy'],
            'command': ['run', '--config', '/etc/caddy/caddy.json'],
            'depends_on': ['nimbo', 'livekit'],
            'environment': {'XDG_DATA_HOME': '/data', 'XDG_CONFIG_HOME': '/config'},
            'volumes': ['./caddy.json:/etc/caddy/caddy.json:ro', 'caddy_data:/data', 'caddy_config:/config'] +
                       (['./private/certificates:/certificates:ro'] if a.certificate_directory else []),
            'tmpfs': ['/tmp:size=32m,mode=1777'], 'cap_add': ['NET_BIND_SERVICE'],
            'pids_limit': 256, 'mem_limit': '256m', 'stop_grace_period': '20s'}},
        'volumes': {'caddy_data': {}, 'caddy_config': {}}}
    return env, livekit, caddy, compose, origin


def preflight(a, bundle):
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64') or os.geteuid() != 0:
        raise ValueError('This bundle requires Linux amd64 and root (sudo).')
    if not re.fullmatch(r'[a-z][a-z0-9-]{2,39}', a.project):
        raise ValueError('Use a unique lowercase project name (3–40 letters/digits/hyphens).')
    if not a.directory.is_absolute() or a.directory.exists():
        raise ValueError('Choose a new absolute installation directory; existing installations are never overwritten.')
    if len({a.domain, a.rtc_domain, a.turn_domain}) != 3:
        raise ValueError('API, signalling and TURN require three different DNS names.')
    if not 1 <= a.https_port <= 65535 or (a.https_port != 443 and not a.certificate_directory):
        raise ValueError('Automatic certificates require port 443; a custom HTTPS port requires supplied certificates.')
    tcp = [a.api_port, a.signal_port, a.rtc_tcp_port, a.turn_tls_port, a.https_port]
    if not a.certificate_directory:
        tcp.append(80)
    if len(tcp) != len(set(tcp)) or a.rtc_udp_port == a.turn_udp_port:
        raise ValueError('Ports of the same transport must be different.')
    for kind, ports in ((socket.SOCK_STREAM, tcp), (socket.SOCK_DGRAM, [a.rtc_udp_port, a.turn_udp_port])):
        for number in ports:
            with socket.socket(socket.AF_INET, kind) as check:
                check.bind(('0.0.0.0', number))
    if not a.skip_dns_check:
        for host in (a.domain, a.rtc_domain, a.turn_domain):
            if str(a.public_ip) not in {r[4][0] for r in socket.getaddrinfo(host, None, socket.AF_INET)}:
                raise ValueError(f'{host} does not resolve to {a.public_ip}')
    if a.certificate_directory:
        for name, host in (('app', a.domain), ('rtc', a.rtc_domain), ('turn', a.turn_domain)):
            crt, key = (a.certificate_directory / f'{name}.{ext}' for ext in ('crt', 'key'))
            run('openssl', 'x509', '-in', str(crt), '-checkend', '86400', '-noout', stdout=subprocess.DEVNULL)
            run('openssl', 'x509', '-in', str(crt), '-checkhost', host, '-noout', stdout=subprocess.DEVNULL)
            pub_cert = run('openssl', 'x509', '-in', str(crt), '-pubkey', '-noout', capture_output=True).stdout
            pub_key = run('openssl', 'pkey', '-in', str(key), '-pubout', capture_output=True).stdout
            if pub_cert != pub_key:
                raise ValueError(f'Certificate/key mismatch: {name}')
    for filename in ('nimbo-server', 'nimbo', 'Dockerfile.binary', 'SHA256SUMS'):
        if not (bundle / filename).is_file():
            raise ValueError(f'Missing release file: {filename}')
    expected = (bundle / 'SHA256SUMS').read_text().split()[0]
    if hashlib.sha256((bundle / 'nimbo-server').read_bytes()).hexdigest() != expected:
        raise ValueError('Server checksum mismatch; refusing to install.')
    run('docker', 'info', '--format', '{{.OSType}}', stdout=subprocess.DEVNULL)
    run('docker', 'compose', 'version', stdout=subprocess.DEVNULL)
    existing = run('docker', 'ps', '-aq', '--filter', f'label=com.docker.compose.project={a.project}', capture_output=True).stdout
    volumes = run('docker', 'volume', 'ls', '-q', '--filter', f'label=com.docker.compose.project={a.project}', capture_output=True).stdout
    if existing.strip() or volumes.strip():
        raise ValueError('That Compose project already owns resources. Choose another project name.')


def write(path, text):
    with path.open('x', encoding='utf-8', newline='\n') as f:
        f.write(text)
    path.chmod(0o600)


def main():
    a = arguments()
    bundle = Path(__file__).resolve().parent
    preflight(a, bundle)
    os.umask(0o077)
    root = a.directory
    root.mkdir(mode=0o700, parents=True, exist_ok=False)
    (root / 'private').mkdir(mode=0o700)
    (root / 'data').mkdir(mode=0o700)
    os.chown(root / 'data', 65532, 65532)
    for filename in ('nimbo-server', 'nimbo', 'Dockerfile.binary', 'SHA256SUMS'):
        shutil.copyfile(bundle / filename, root / filename)
    (root / 'nimbo').chmod(0o700)
    (root / 'nimbo-server').chmod(0o755)
    if a.certificate_directory:
        dest = root / 'private/certificates'
        dest.mkdir(mode=0o700)
        for name in ('app', 'rtc', 'turn'):
            for ext in ('crt', 'key'):
                shutil.copyfile(a.certificate_directory / f'{name}.{ext}', dest / f'{name}.{ext}')
    env, livekit, caddy, compose, origin = configuration(a)
    write(root / 'private/nimbo.env', env)
    for path, value in [('private/livekit.yaml', livekit), ('caddy.json', caddy), ('compose.json', compose)]:
        write(root / path, json.dumps(value, indent=2) + '\n')
    write(root / 'installation.json', json.dumps({'project': a.project, 'origin': origin,
          'apiPort': a.api_port, 'suppliedCertificates': bool(a.certificate_directory),
          'webPreview': a.web_preview}, indent=2) + '\n')
    print(f'Prepared {root}. Public ports: TCP {a.https_port}, {a.rtc_tcp_port}; UDP {a.rtc_udp_port}, {a.turn_udp_port}.')
    print('Firewall rules and existing installations were not changed.')
    if not a.certificate_directory:
        print('TCP 80 is also required for automatic HTTPS. Check A and AAAA DNS records.')
    else:
        print(f'TURN TLS also requires TCP {a.turn_tls_port}.')
        print('Supplied certificates are copies; arrange renewal before they expire.')
    if not a.prepare_only:
        run(str(root / 'nimbo'), 'install')
    print(f'Control this installation: sudo {root}/nimbo help')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(f'Installation stopped: {error}')
