#!/usr/bin/env python3
"""Reject stale ON configurations, hidden models, and dependency graph leaks."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wake_gate', ROOT / 'scripts/check_kizz_disabled_build.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class DisabledWakeBuildTests(unittest.TestCase):
    def test_rejects_compiler_and_linker_leaks_independently(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            commands = [{'file': '/repo/components/m5_platform/m5_platform.cpp',
                         'command': 'c++ -DHIPHI_KIZZ_WAKE_WORD=0 -c m5_platform.cpp'}]
            def write(entries=commands, link_map='m5_platform_voice_get_phase .text'):
                (root / 'compile_commands.json').write_text(json.dumps(entries))
                (root / 'hiphi_stackchan.map').write_text(link_map)
            write()
            gate.check_build(root, 'hiphi_stackchan')
            for token in gate.FORBIDDEN:
                with self.subTest(token=token):
                    write(commands + [{'file': f'/repo/{token}/model.cpp', 'command': 'c++ -c model.cpp'}])
                    with self.assertRaises(ValueError): gate.check_build(root, 'hiphi_stackchan')
                    write(link_map=f'archive: {token}')
                    with self.assertRaises(ValueError): gate.check_build(root, 'hiphi_stackchan')
            for flag in ('', '-DHIPHI_KIZZ_WAKE_WORD=1', '-DHIPHI_KIZZ_WAKE_WORD=0 -DHIPHI_KIZZ_WAKE_WORD=1'):
                write([{'file': 'm5_platform.cpp', 'command': 'c++ ' + flag}])
                with self.assertRaises(ValueError): gate.check_build(root, 'hiphi_stackchan')
            write([])
            with self.assertRaises(ValueError): gate.check_build(root, 'hiphi_stackchan')
            write()
            (root / 'hiphi_stackchan.map').unlink()
            with self.assertRaises(FileNotFoundError): gate.check_build(root, 'hiphi_stackchan')


if __name__ == '__main__':
    unittest.main()
