"""Configuration tests: no live keys, containers or installations."""
import contextlib
import importlib.util
import importlib.machinery
import io
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parent))
import host

spec = importlib.util.spec_from_file_location('installer', Path(__file__).with_name('install.py'))
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
GIB = 1024**3


class InstallerTests(unittest.TestCase):
    def choose(self, *args, **kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            return installer.select_antivirus(*args, **kwargs)

    def test_four_gib_defaults_on_without_prompt(self):
        self.assertTrue(self.choose('auto', 4 * GIB, interactive=False))

    def test_low_or_unknown_memory_requires_explicit_unattended_choice(self):
        for ram in (None, 2 * GIB, 4 * GIB - 1):
            with self.assertRaisesRegex(ValueError, '--antivirus'):
                self.choose('auto', ram, interactive=False)

    def test_low_memory_choice_has_no_implicit_default(self):
        answers = iter(['', 'maybe', 'да'])
        self.assertTrue(self.choose('auto', 2 * GIB, interactive=True, ask=lambda _: next(answers)))
        self.assertFalse(self.choose('auto', 2 * GIB, interactive=True, ask=lambda _: 'нет'))

    def test_explicit_flags(self):
        for ram in (None, 2 * GIB, 8 * GIB):
            self.assertTrue(self.choose('on', ram, interactive=False))
            self.assertFalse(self.choose('off', ram, interactive=False))

    def config(self, enabled):
        argv = ['install.py', '--domain', 'chat.example.org', '--rtc-domain', 'rtc.example.org',
                '--turn-domain', 'turn.example.org', '--public-ip', '203.0.113.10', '--https-port', '7443']
        with patch.object(sys, 'argv', argv):
            args = installer.arguments()
        args.antivirus_enabled = enabled
        args.project = 'nimbo-7443'
        with patch.object(host, 'free_port', return_value=True):
            host.allocate_ports(args, {})
        return installer.configuration(args)

    def test_scanner_isolation(self):
        env, _, _, compose, _ = self.config(True)
        scanner = compose['services']['antivirus']
        for field in ('ports', 'network_mode', 'privileged', 'env_file'):
            self.assertNotIn(field, scanner)
        self.assertEqual(scanner['user'], '65532:65532')
        self.assertTrue(scanner['read_only'])
        self.assertEqual(scanner['cap_drop'], ['ALL'])
        self.assertEqual(scanner['volumes'], [host.bind('./storage/antivirus-db', '/var/lib/clamav'), './antivirus/run:/run/nimbo-antivirus'])
        self.assertIn('NIMBO_CLAMD_ADDRESS=unix:', env)
        self.assertIn('NIMBO_ANTIVIRUS_MANAGER_SOCKET=', env)
        self.assertIn('./antivirus/run:/run/nimbo-antivirus:ro', compose['services']['nimbo']['volumes'])
        self.assertIn('healthcheck', scanner)

    def test_opt_out_has_no_phantom_scanner(self):
        env, _, _, compose, _ = self.config(False)
        self.assertNotIn('antivirus', compose['services'])
        self.assertNotIn('NIMBO_CLAMD', env)
        self.assertNotIn('NIMBO_ANTIVIRUS_MANAGER', env)
        self.assertEqual(compose['services']['nimbo']['volumes'], [host.bind('./storage/data', '/var/lib/nimbo')])

    def test_memory_uses_smaller_daemon_capacity_not_free_memory(self):
        with patch.object(installer.Path, 'read_text', return_value='MemTotal: 8388608 kB\nMemAvailable: 100 kB\n'), patch.object(installer, 'run') as command:
            command.return_value.stdout = str(2 * GIB).encode()
            self.assertEqual(installer.memory_bytes(), 2 * GIB)


class ReadinessTests(unittest.TestCase):
    def setUp(self):
        loader = importlib.machinery.SourceFileLoader('control', str(Path(__file__).with_name('nimbo')))
        spec = importlib.util.spec_from_loader('control', loader)
        self.control = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {'fcntl': types.SimpleNamespace()}):
            loader.exec_module(self.control)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.control.ROOT = Path(self.temp.name)
        self.settings = self.control.ROOT / 'installation.json'
        self.settings.write_text('{"antivirus":true}')

    def test_no_scanner_skips_wait(self):
        self.settings.write_text('{"antivirus":false}')
        with patch.object(self.control, 'compose') as compose:
            self.control.antivirus_ready()
            compose.assert_not_called()

    def test_waits_for_health_not_just_running_container(self):
        for body in ('[{"Health":"healthy"}]', '{"Health":"healthy"}\n'):
            with patch.object(self.control, 'compose') as compose, patch.object(self.control.time, 'sleep') as sleep, contextlib.redirect_stdout(io.StringIO()):
                compose.side_effect = [types.SimpleNamespace(stdout='{"Health":"starting"}'), types.SimpleNamespace(stdout=body)]
                self.control.antivirus_ready()
                self.assertEqual(compose.call_count, 2)
                sleep.assert_called_once_with(5)

    def test_unavailable_scanner_fails_without_mutating_policy(self):
        with patch.object(self.control, 'compose') as compose, patch.object(self.control.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            compose.return_value.stdout = '{"Health":"unhealthy"}'
            with self.assertRaisesRegex(ValueError, 'НЕ отключена'):
                self.control.antivirus_ready()
            self.assertEqual(compose.call_count, 120)
            self.assertTrue(all(call.args == ('ps', '--format', 'json', 'antivirus') for call in compose.call_args_list))


class HostTests(unittest.TestCase):
    def args(self, **values):
        defaults = dict(project='nimbo-7443', directory=Path('/opt/nimbo-7443'), https_port=7443,
                        memory_mib=1536, cpus=2.0, storage_gib=10, antivirus_enabled=False,
                        certificate_directory=None, firewall='manual')
        defaults.update({field: None for field in host.PORT_FIELDS})
        defaults.update(values)
        return types.SimpleNamespace(**defaults)

    def test_noninteractive_port_must_be_explicit(self):
        with self.assertRaisesRegex(ValueError, '--https-port'):
            host.select_port(None, {}, interactive=False)

    def test_prompt_suggests_free_port_but_needs_answer(self):
        with patch.object(host, 'free_port', side_effect=lambda n, udp=False: n != 7443):
            prompts = []
            value = host.select_port(None, {}, interactive=True, ask=lambda p: prompts.append(p) or '')
            self.assertEqual(value, 9443)
            self.assertIn('9443', prompts[0])

    def test_reserved_port_is_unavailable_even_when_no_listener(self):
        with patch.object(host, 'free_port', return_value=True):
            with self.assertRaises(ValueError):
                host.select_port(7443, {'stopped': {'tcp': [7443]}}, interactive=False)

    def test_auto_allocation_honours_explicit_ports_and_stopped_records(self):
        args = self.args(turn_tls_port=20001)
        with patch.object(host, 'free_port', return_value=True):
            host.allocate_ports(args, {'stopped': {'tcp': [20000], 'udp': [20000]}})
        self.assertEqual(args.turn_tls_port, 20001)
        self.assertEqual(args.api_port, 20002)
        self.assertEqual(args.rtc_udp_port, 20001)
        self.assertNotEqual(args.turn_udp_port, args.rtc_udp_port)

    def test_duplicate_explicit_ports_rejected(self):
        with patch.object(host, 'free_port', return_value=True):
            with self.assertRaises(ValueError):
                host.allocate_ports(self.args(api_port=24000, signal_port=24000), {})

    def test_two_installations_have_disjoint_allocations(self):
        first, second = self.args(), self.args(https_port=9443)
        with patch.object(host, 'free_port', return_value=True):
            host.allocate_ports(first, {})
            records = {'first': {'tcp': [first.https_port, first.api_port, first.signal_port, first.rtc_tcp_port, first.turn_tls_port],
                                 'udp': [first.rtc_udp_port, first.turn_udp_port]}}
            host.allocate_ports(second, records)
        self.assertTrue(set(records['first']['tcp']).isdisjoint({second.https_port, second.api_port, second.signal_port, second.rtc_tcp_port, second.turn_tls_port}))
        self.assertTrue(set(records['first']['udp']).isdisjoint({second.rtc_udp_port, second.turn_udp_port}))

    def test_memory_budgets_include_scanner_other_instances_and_os(self):
        with patch.object(host.os, 'cpu_count', return_value=8), patch.object(host.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=100 * GIB)), contextlib.redirect_stdout(io.StringIO()):
            host.resource_preflight(self.args(), {}, 4 * GIB)
            with self.assertRaisesRegex(ValueError, 'ОЗУ'):
                host.resource_preflight(self.args(antivirus_enabled=True), {}, 4 * GIB)
            with self.assertRaisesRegex(ValueError, 'ОЗУ'):
                host.resource_preflight(self.args(), {'other': {'directory': '/opt/other', 'memoryMiB': 2500, 'cpus': 1}}, 4 * GIB)

    def test_cpu_overcommit_and_nonfinite_values_rejected(self):
        with patch.object(host.os, 'cpu_count', return_value=2):
            for cpus in (float('nan'), float('inf'), -.1, 3):
                with self.assertRaises(ValueError):
                    host.resource_preflight(self.args(cpus=cpus), {}, 8 * GIB)

    def test_storage_is_reserved_with_headroom(self):
        with patch.object(host.os, 'cpu_count', return_value=4), patch.object(host.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=12 * GIB)):
            with self.assertRaisesRegex(ValueError, 'диске'):
                host.resource_preflight(self.args(storage_gib=10), {}, 8 * GIB)

    def test_composed_limits_sum_to_selected_budget_and_no_swap(self):
        for enabled in (False, True):
            env, livekit, caddy, compose, origin = InstallerTests().config(enabled)
            services = compose['services']
            self.assertAlmostEqual(sum(s['cpus'] for s in services.values()), 2)
            for s in services.values():
                self.assertEqual(s['mem_limit'], s['memswap_limit'])
            self.assertEqual(origin, 'https://chat.example.org:7443')
            self.assertFalse(livekit['turn']['external_tls'])
            self.assertEqual(caddy['apps']['http']['servers']['nimbo']['listen'], [':7443'])
            self.assertTrue(caddy['apps']['http']['servers']['nimbo']['automatic_https']['disable'])
            self.assertFalse(services['nimbo']['volumes'][0]['bind']['create_host_path'])

    def test_acme_uses_private_state_and_webroot_without_shell(self):
        settings = {'project': 'nimbo-7443', 'domains': ['a.example.org', 'b.example.org', 'c.example.org'], 'acmeWebroot': '/srv/acme'}
        cmd = host.certificate_command(settings, Path('/opt/nimbo-7443'))
        self.assertIn('--webroot', cmd)
        self.assertIn(str(Path('/opt/nimbo-7443') / 'private/acme'), cmd)
        self.assertNotIn('--standalone', cmd)
        self.assertEqual(cmd.count('-d'), 3)
        renew = host.certificate_command(settings, Path('/opt/nimbo-7443'), renew=True)
        self.assertEqual(renew[1], 'renew')
        self.assertNotIn('--agree-tos', renew)

    def test_existing_disk_image_never_reformatted(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'storage.ext4').write_bytes(b'data')
            with patch.object(host, 'run') as command:
                with self.assertRaises(ValueError):
                    host.prepare_storage(root, 2)
                command.assert_not_called()


if __name__ == '__main__':
    unittest.main()
