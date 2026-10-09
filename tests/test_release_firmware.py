#!/usr/bin/env python3
"""Exercise release preparation against local Git repos; never GitHub."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
APPS = ('idf_app', 'frame_app', 'rlcd_app', 'atom_app', 'tough_app', 'halo_app', 'm5_beta_app', 'knob_aux_app')


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / 'repo'
        self.remote = Path(self.temp.name) / 'remote.git'
        self.repo.mkdir()
        self.env = {**os.environ, 'GIT_CONFIG_NOSYSTEM': '1', 'GIT_CONFIG_GLOBAL': os.devnull,
                    'GIT_AUTHOR_NAME': 'Release test', 'GIT_AUTHOR_EMAIL': 'test@example.invalid',
                    'GIT_COMMITTER_NAME': 'Release test', 'GIT_COMMITTER_EMAIL': 'test@example.invalid'}
        self.git('init', '-b', 'main')
        self.git('init', '--bare', str(self.remote))
        (self.repo / 'scripts').mkdir()
        shutil.copyfile(ROOT / 'scripts/release_firmware.sh', self.repo / 'scripts/release_firmware.sh')
        for app in APPS:
            (self.repo / app).mkdir()
            (self.repo / app / 'CMakeLists.txt').write_text('set(PROJECT_VER "2.7.0")\n')
        self.git('add', '.')
        self.git('commit', '-m', 'fixture')
        self.git('remote', 'add', 'origin', str(self.remote))
        self.git('push', '-u', 'origin', 'main')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.repo, env=self.env, stderr=subprocess.STDOUT, text=True).strip()

    def release(self, version):
        return subprocess.run(['bash', 'scripts/release_firmware.sh', version], cwd=self.repo, env=self.env, capture_output=True, text=True)

    def test_supported_channel_updates_every_app_and_publishes_matching_tag(self):
        result = self.release('2.8.0-beta.1')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for app in APPS:
            self.assertEqual((self.repo / app / 'CMakeLists.txt').read_text(), 'set(PROJECT_VER "2.8.0-beta.1")\n')
        self.assertEqual(self.git('rev-parse', 'HEAD'), self.git('rev-parse', 'v2.8.0-beta.1^{}'))
        self.assertIn('refs/tags/v2.8.0-beta.1', self.git('ls-remote', '--tags', 'origin'))
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_unsupported_channels_do_not_mutate_files_commits_or_tags(self):
        head = self.git('rev-parse', 'HEAD')
        for version in ('2.8.0-rc.1', '2.8.0-beta', '2.8.0-alpha.1.extra', '2.8.0+build'):
            with self.subTest(version=version):
                self.assertNotEqual(self.release(version).returncode, 0)
                self.assertEqual(self.git('rev-parse', 'HEAD'), head)
                self.assertEqual(self.git('status', '--porcelain'), '')
                self.assertEqual(self.git('tag'), '')

    def test_missing_version_fails_before_partially_updating_apps(self):
        (self.repo / 'knob_aux_app/CMakeLists.txt').write_text('project(aux)\n')
        self.git('add', '.')
        self.git('commit', '-m', 'malformed fixture')
        self.assertNotEqual(self.release('2.8.0').returncode, 0)
        self.assertEqual(self.git('status', '--porcelain'), '')
        self.assertEqual(self.git('tag'), '')


if __name__ == '__main__':
    unittest.main()
