"""Check shell dispatch and environment defaults without Docker or Tarantool.

Fake commands capture argv and environment; privilege dropping is simulated.
Run with python3 on a Linux host.
"""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


DOCKER_DIR = Path(__file__).resolve().parents[1]
DEFAULTS = (
    'TT_INSTANCE_NAME', 'TT_CONFIG', 'TT_SNAPSHOT_DIR_DEFAULT',
    'TT_VINYL_DIR_DEFAULT', 'TT_WAL_DIR_DEFAULT', 'TT_LOG_FILE_DEFAULT',
    'TT_CONSOLE_SOCKET_DEFAULT', 'TT_PROCESS_PID_FILE_DEFAULT',
)
FIXTURES = Path(__file__).resolve().with_name('fixtures')


class EntrypointTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        bin_dir = Path(directory.name)
        self.capture = bin_dir / 'capture.json'
        for name in ('id', 'gosu', 'tarantool', 'probe', 'tt'):
            (bin_dir / name).symlink_to(FIXTURES / 'command.py')

    def invoke(self, *args, settings=None, script=None):
        """Run a real shell script and return the fake command's capture."""
        # Host TT_* settings must not influence the defaults under test.
        env = {
            'PATH': f'{self.capture.parent}:/usr/bin:/bin',
            'TT_APP_NAME': 'default',
            'CAPTURE_FILE': str(self.capture),
        }
        env.update(settings or {})
        self.capture.unlink(missing_ok=True)
        script = script or DOCKER_DIR / 'entrypoint/docker-entrypoint.sh'
        command = ['/bin/sh', str(script), *args]
        try:
            result = subprocess.run(command, env=env, text=True,
                                    capture_output=True, timeout=5)
        except subprocess.TimeoutExpired as error:
            self.fail(f'{command!r} timed out after {error.timeout}s\n'
                      f'stdout: {error.stdout!r}\nstderr: {error.stderr!r}')
        diagnostics = (f'{command!r} exited with {result.returncode}\n'
                       f'stdout: {result.stdout!r}\nstderr: {result.stderr!r}')
        self.assertEqual(result.returncode, 0, diagnostics)
        self.assertTrue(self.capture.is_file(), diagnostics)
        return json.loads(self.capture.read_text())

    def test_utility_receives_no_automatic_settings(self):
        result = self.invoke('probe', 'argument with spaces', '')
        self.assertEqual(result['argv'], ['argument with spaces', ''])
        for key in DEFAULTS:
            self.assertNotIn(key, result['env'])

    def test_non_server_actions_receive_no_automatic_settings(self):
        cases = [
            ('tool.lua', '--name', 'ignored'), ('-',), ('--', 'tool.lua'),
            ('-e', 'print("hello")'), ('-eprint("hello")',),
            ('-ieprint("hello")',), ('-i', '-e', 'return'),
            ('-b', 'tool.lua', 'tool.luac'), ('-bg', 'tool.lua', 'out'),
            ('-l', 'json', 'tool.lua'), ('-ljson', 'tool.lua'),
            ('-j', 'off', '-'), ('-joff', '-'),
            ('--name', 'chosen', 'tool.lua'),
            ('--config=chosen.yaml', '-e', 'return'),
            ('--help',), ('--version',), ('--help-env-list',),
            ('-v',), ('-V',), ('-ih',), ('--unknown',),
            ('--integrity-check', 'hashes'), ('--failover',),
            ('-i', 'tool.lua'), ('--', '-e'), ('',),
            ('-c',), ('-n',), ('-l',), ('-j',), ('--config',), ('--name',),
            ('--force-invalid',), ('--force-recovery=yes',),
        ]
        for args in cases:
            with self.subTest(args=args):
                result = self.invoke('tarantool', *args)
                self.assertEqual(result['argv'], list(args))
                for key in DEFAULTS:
                    self.assertNotIn(key, result['env'])

    def test_server_defaults_and_option_operands(self):
        cases = [
            (), ('-i',), ('-id',), ('--',),
            ('--config', 'file with spaces.yaml'), ('--config=-e',),
            ('-c', '-e'), ('-cconfig.yaml',),
            ('-l', 'json'), ('-ljson',), ('-iljson',),
            ('-j', 'off'), ('-joff',), ('-l', '-e'),
            ('--force-recovery',), ('--force-reco',),
            ('--conf', 'file.yaml'),
        ]
        for args in cases:
            with self.subTest(args=args):
                result = self.invoke('tarantool', *args)
                self.assertEqual(result['argv'], list(args))
                self.assertEqual(result['env']['TT_INSTANCE_NAME'],
                                 'instance-001')
                self.assertEqual(result['env']['TT_CONFIG'],
                                 '/default-config.yaml')
                self.assertEqual(result['env']['TT_WAL_DIR_DEFAULT'],
                                 '/var/lib/tarantool/sys_env/default/'
                                 'instance-001')

    def test_cli_name_retains_image_path_defaults(self):
        for args in [('--name', 'chosen'), ('--name=chosen',),
                     ('-nchosen',), ('-in', 'chosen'),
                     ('-nfirst', '--nam', 'chosen')]:
            with self.subTest(args=args):
                result = self.invoke('tarantool', *args)
                self.assertEqual(result['argv'], list(args))
                self.assertEqual(result['env']['TT_CONSOLE_SOCKET_DEFAULT'],
                                 '/var/run/tarantool/sys_env/default/'
                                 'instance-001/tarantool.control')

    def test_explicit_settings_survive_all_modes_and_reexecution(self):
        settings = {key: f'explicit {key}' for key in DEFAULTS}
        settings.update(TT_APP_NAME='custom', TT_LOG='stderr',
                        TT_CONSOLE_SOCKET='/explicit.control')
        for args in [('probe',), ('tarantool',),
                     ('tarantool', '--name=cli'),
                     ('tarantool', '-e', 'return'),
                     ('tarantool', 'tool.lua')]:
            for uid in ('0', '1000'):
                with self.subTest(args=args, uid=uid):
                    result = self.invoke(*args, settings={
                        **settings, 'FAKE_UID': uid,
                    })
                    for key, value in settings.items():
                        self.assertEqual(result['env'][key], value)
                    if uid == '0' and args[0] == 'tarantool':
                        self.assertEqual(
                            result['env']['DROPPED_PRIVILEGES'], 'yes')

    def test_empty_settings_are_not_unset(self):
        settings = {key: '' for key in DEFAULTS if key != 'TT_CONFIG'}
        for config in ({}, {'TT_CONFIG': ''}):
            for uid in ('0', '1000'):
                with self.subTest(config=config, uid=uid):
                    result = self.invoke('tarantool', settings={
                        **settings, **config, 'FAKE_UID': uid,
                    })
                    for key in settings:
                        self.assertEqual(result['env'][key], '')
                    self.assertEqual(result['env']['TT_CONFIG'],
                                     '/default-config.yaml')

    def test_leading_options_preserve_arguments(self):
        result = self.invoke('-e', 'print("spaces and * glob")', '')
        self.assertEqual(result['argv'], ['-e', 'print("spaces and * glob")',
                                          ''])
        self.assertNotIn('TT_INSTANCE_NAME', result['env'])

    def test_helpers_have_independent_defaults(self):
        for helper in ('console', 'status'):
            for settings, expected in [
                ({}, '/var/run/tarantool/sys_env/default/'
                 'instance-001/tarantool.control'),
                ({'TT_APP_NAME': 'app', 'TT_INSTANCE_NAME': 'chosen'},
                 '/var/run/tarantool/sys_env/app/chosen/tarantool.control'),
                ({'TT_CONSOLE_SOCKET': '/override.control'},
                 '/override.control'),
                ({'TT_CONSOLE_SOCKET_DEFAULT': '/default.control'},
                 '/default.control'),
            ]:
                with self.subTest(helper=helper, settings=settings):
                    result = self.invoke(settings=settings,
                                         script=DOCKER_DIR / 'tools' / helper)
                    self.assertEqual(result['argv'][0:2],
                                     ['connect', expected])


if __name__ == '__main__':
    unittest.main(verbosity=2)
