import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('audit', Path(__file__).parents[1] / 'scripts/audit_firmware_stack.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

class AuditTests(unittest.TestCase):
    def test_nested_calls_and_unknowns_are_not_silently_safe(self):
        functions = audit.parse('''00000000 <root_handler>:
 0: 000 entry a1, 0x100
 3: 000 call8 10 <child>
00000010 <child>:
 10: 000 entry a1, 64
 13: 000 callx8 a8
 16: 000 call8 0 <root_handler>
''')
        row = audit.report(functions)['entry_paths'][0]
        self.assertEqual(row['known_chain_bytes'], 320)
        self.assertIn('recursion', row['gaps'])
        self.assertIn('indirect call', row['gaps'])
    def test_unknown_frame_is_reported(self):
        row = audit.report(audit.parse('00000000 <test_handler>:\n 0: 000 retw.n\n'))['entry_paths'][0]
        self.assertIn('unknown frame', row['gaps'])

if __name__ == '__main__': unittest.main()
