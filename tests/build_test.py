"""Verify source provenance and refusal to launch stale checkout binaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
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


if __name__ == '__main__':
    unittest.main()
