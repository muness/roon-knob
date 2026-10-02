#!/usr/bin/env python3
"""Run the actual publication copy functions against all M5 artifact layouts."""
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / '.github/workflows/docker.yml').read_text()
TARGETS = {'dial': 'm5dial', 'sticks3': 'sticks3', 'stopwatch': 'stopwatch', 'stackchan': 'stackchan'}


class FirmwareProjectTests(unittest.TestCase):
    def test_every_publish_path_uses_the_distinct_m5_project(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for directory in ('release-assets', 'pr-preview', 'gh-pages-build'):
                (root / directory).mkdir()
            for target, project in TARGETS.items():
                base = root / 'm5-beta-artifacts/m5_beta_app' / f'build-{target}'
                for path in (f'hiphi_{project}.bin', 'bootloader/bootloader.bin', 'partition_table/partition-table.bin', 'ota_data_initial.bin'):
                    artifact = base / path
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.write_text(f'{target}/{path}')
                (root / 'm5-beta-artifacts' / f'hiphi_{target}_merged.bin').write_text(f'{target}/merged')
            for name in ('prepare_m5_release', 'prepare_m5_beta', 'prepare_m5_pages'):
                match = re.search(r'^          ' + name + r'\(\) \{.*?^          \}(?=\n\s*prepare_m5_)', WORKFLOW, re.M | re.S)
                self.assertIsNotNone(match, name)
                script = 'set -eu\nPREVIEW_ID=fixture\nPR_NUM=265\n' + textwrap.dedent(match[0]) + '\n'
                script += '\n'.join(f'{name} {target} {page} "HiPhi fixture"' for target, page in TARGETS.items())
                subprocess.run(['bash', '-c', script], cwd=root, check=True)
            for target, project in TARGETS.items():
                expected = f'{target}/hiphi_{project}.bin'
                for output in ('release-assets', 'gh-pages-build'):
                    self.assertEqual((root / output / f'hiphi_{project}.bin').read_text(), expected)
                self.assertEqual((root / 'pr-preview' / f'hiphi_{project}-fixture.bin').read_text(), expected)

    def test_build_and_package_paths_use_matrix_project_identity(self):
        for target, project in TARGETS.items():
            self.assertIn(f'- target: {target}\n            project: hiphi_{project}', WORKFLOW)
        self.assertNotIn('build-${{ matrix.target }}/hiphi_${{ matrix.target }}.bin', WORKFLOW)
        self.assertNotIn('build-${{ matrix.target }}/hiphi_${{ matrix.target }}.elf', WORKFLOW)
        self.assertIn('build-${{ matrix.target }}/${{ matrix.project }}.elf', WORKFLOW)
        self.assertIn('build-${{ matrix.target }}/${{ matrix.project }}.bin', WORKFLOW)


if __name__ == '__main__':
    unittest.main()
