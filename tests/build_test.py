"""Verify source provenance and refusal to launch stale checkout binaries."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

SOURCE = Path(__file__).resolve().parents[1]


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='blazer-build-test-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'checkout with spaces'
        self.source.mkdir()
        shutil.copytree(SOURCE / 'cmake', self.source / 'cmake')
        self.binary = self.root / 'output with spaces'

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.source), *args],
                                       text=True, stderr=subprocess.DEVNULL).strip()

    def commit(self):
        self.git('add', '.')
        self.git('-c', 'user.name=Build Test', '-c', 'user.email=build@example.invalid',
                 'commit', '-qm', 'fixture')
        return self.git('rev-parse', 'HEAD')

    def generate(self, source=None, check=True):
        result = subprocess.run(['cmake', f'-DBLAZER_SOURCE_DIR={source or self.source}',
                                 f'-DBLAZER_BINARY_DIR={self.binary}',
                                 '-P', str(SOURCE / 'cmake/BuildInfo.cmake')],
                                capture_output=True, text=True, check=check)
        return result

    def header(self):
        return (self.binary / 'generated/blazer_version.h').read_text()

    def test_git_revision_dirty_and_new_commit_without_reconfigure(self):
        self.git('init', '-q')
        revision = self.commit()
        self.generate()
        self.assertIn(f'"{revision}"', self.header())
        (self.source / 'change.txt').write_text('changed')
        self.generate()
        self.assertIn(f'"{revision}-dirty"', self.header())
        timestamp = (self.binary / 'generated/blazer_version.h').stat().st_mtime_ns
        self.generate()
        self.assertEqual(timestamp, (self.binary / 'generated/blazer_version.h').stat().st_mtime_ns)
        next_revision = self.commit()
        self.generate()
        self.assertNotEqual(revision, next_revision)
        self.assertIn(f'"{next_revision}"', self.header())

    def test_git_worktree_file(self):
        self.git('init', '-q')
        revision = self.commit()
        worktree = self.root / 'worktree'
        self.git('worktree', 'add', '--detach', str(worktree))
        self.assertTrue((worktree / '.git').is_file())
        self.generate(worktree)
        self.assertIn(f'"{revision}"', self.header())

    def test_source_archive_revision_and_invalid_input(self):
        revision = 'a1' * 20
        (self.source / 'SOURCE_REVISION').write_text(revision + '\n')
        self.generate()
        self.assertIn(f'"{revision}"', self.header())
        (self.source / 'SOURCE_REVISION').write_text('abc\nmalformed')
        self.assertNotEqual(self.generate(check=False).returncode, 0)
        # Failed provenance must not replace the last good generated header.
        self.assertIn(f'"{revision}"', self.header())

    def test_unversioned_sources_are_explicit(self):
        self.generate()
        self.assertIn('"source-archive"', self.header())

    def launcher_fixture(self):
        shutil.copy(SOURCE / 'run.sh', self.source / 'run.sh')
        build = self.source / 'build'
        build.mkdir()
        (build / 'CMakeCache.txt').touch()
        app = build / 'blazer-studio'
        app.write_text('#!/bin/sh\nprintf "app:%s\\n" "$*" >> "$BLAZER_TEST_LOG"\n')
        app.chmod(0o755)
        binaries = self.root / 'bin'
        binaries.mkdir()
        cmake = binaries / 'cmake'
        cmake.write_text('#!/bin/sh\nprintf "cmake:%s\\n" "$*" >> "$BLAZER_TEST_LOG"\n'
                         'if [ "$1" = --build ] && [ "$BLAZER_TEST_FAIL" = 1 ]; then exit 42; fi\n')
        cmake.chmod(0o755)
        self.log = self.root / 'log'
        return {**os.environ, 'PATH': str(binaries) + ':' + os.environ['PATH'],
                'BLAZER_TEST_LOG': str(self.log), 'BLAZER_TEST_FAIL': '0'}

    def test_existing_binary_is_built_before_launch(self):
        env = self.launcher_fixture()
        subprocess.run(['bash', str(self.source / 'run.sh'), 'file with spaces.blazer'],
                       env=env, check=True)
        lines = self.log.read_text().splitlines()
        self.assertEqual(len(lines), 3)
        self.assertTrue(lines[0].startswith('cmake:-S '))
        self.assertTrue(lines[1].startswith('cmake:--build '))
        self.assertIn('--target blazer-studio', lines[1])
        self.assertEqual(lines[2], 'app:file with spaces.blazer')

    def test_failed_build_refuses_existing_binary(self):
        env = self.launcher_fixture()
        env['BLAZER_TEST_FAIL'] = '1'
        result = subprocess.run(['bash', str(self.source / 'run.sh')], env=env)
        self.assertEqual(result.returncode, 42)
        self.assertNotIn('app:', self.log.read_text())

    def local_install_fixture(self):
        for name in ('install-local.sh',):
            shutil.copy(SOURCE / name, self.source / name)
        (self.source / 'packaging').mkdir()
        for name in ('blazer-studio.desktop.in', 'blazer-studio.xml'):
            shutil.copy(SOURCE / 'packaging' / name, self.source / 'packaging' / name)
        (self.source / 'assets').mkdir()
        (self.source / 'assets/blazer-studio.svg').write_text('<svg/>')
        data = self.root / 'private data'
        env = {**os.environ, 'XDG_DATA_HOME': str(data),
               'XDG_CONFIG_HOME': str(self.root / 'config'),
               'XDG_CACHE_HOME': str(self.root / 'cache')}
        for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
            env.pop(name, None)
        return data, env

    @unittest.skipUnless(shutil.which('gio') and shutil.which('xdg-mime') and
                         shutil.which('update-mime-database'), 'desktop tools not installed')
    def test_local_install_passes_template_path_without_shell_expansion(self):
        self.source = self.source.rename(self.root / 'checkout = % ü $ ` " \\')
        data, env = self.local_install_fixture()
        record = self.root / 'launched.json'
        env['BLAZER_LAUNCH_RECORD'] = str(record)
        (self.source / 'run.sh').write_text('''#!/bin/bash
python3 -c 'import json, os, pathlib, sys; path=pathlib.Path(os.environ["BLAZER_LAUNCH_RECORD"]); temporary=path.with_suffix(".tmp"); temporary.write_text(json.dumps(sys.argv[1:])); temporary.replace(path)' "$@"
''')
        installer = ['bash', str(self.source / 'install-local.sh')]
        subprocess.run(installer, env=env, check=True, capture_output=True)
        desktop = data / 'applications/blazer-studio.desktop'
        before = desktop.read_bytes()
        subprocess.run(installer, env=env, check=True, capture_output=True)
        self.assertEqual(desktop.read_bytes(), before)
        template = self.root / 'project ü # % with spaces.blazertemplate'
        template.write_text('{}')
        mime = subprocess.check_output(['xdg-mime', 'query', 'filetype', str(template)],
                                       env=env, text=True).strip()
        self.assertEqual(mime, 'application/x-blazer-template')
        subprocess.run(['gio', 'launch', str(desktop), str(template)],
                       env=env, check=True, capture_output=True)
        deadline = time.monotonic() + 3
        while not record.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertTrue(record.exists(), 'launcher did not execute the isolated stub')
        self.assertEqual(json.loads(record.read_text()), [str(template)])

    def test_local_install_preserves_foreign_entries(self):
        data, env = self.local_install_fixture()
        launcher = data / 'applications/blazer-studio.desktop'
        launcher.parent.mkdir(parents=True)
        original = '[Desktop Entry]\nExec=/different/application\n'
        launcher.write_text(original)
        installer = ['bash', str(self.source / 'install-local.sh')]
        result = subprocess.run(installer, env=env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(launcher.read_text(), original)
        launcher.unlink()
        mime = data / 'mime/packages/blazer-studio.xml'
        mime.parent.mkdir(parents=True)
        mime.write_text('unrelated file type')
        result = subprocess.run(installer, env=env, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(mime.read_text(), 'unrelated file type')
        self.assertFalse(launcher.exists())


if __name__ == '__main__':
    unittest.main()
