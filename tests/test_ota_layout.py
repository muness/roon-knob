#!/usr/bin/env python3
"""Every OTA-capable target keeps two update slots that real images fit in, and the settings
partition stays where installed devices have it."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
MB = 1024 * 1024
NVS = (0x9000, 0x4000)
# Largest application image published for each target in v2.8.0-alpha.5. A slot must hold it with
# headroom for growth.
LARGEST_IMAGE = {
    'idf_app': 2234368, 'frame_app': 1502688, 'rlcd_app': 1812112, 'atom_app': 1450672,
    'halo_app': 1882752, 'tough_app': 1362480,
    'm5_beta_app:dial': 1456688, 'm5_beta_app:sticks3': 1378864,
    'm5_beta_app:stopwatch': 1379456, 'm5_beta_app:stackchan': 1529152,
}
HEADROOM = 1.05


def parse_table(path):
    rows = {}
    for line in path.read_text().splitlines():
        line = line.split('#')[0].strip()
        if not line:
            continue
        name, kind, sub, offset, size = [c.strip() for c in line.split(',')[:5]]
        rows[name] = (kind, sub, int(offset, 0), int(size, 0))
    return rows


def flash_size(defaults):
    text = '\n'.join(p.read_text() for p in defaults if p.exists())
    match = re.findall(r'^CONFIG_ESPTOOLPY_FLASHSIZE_(\d+)MB=y', text, re.M)
    assert match, defaults
    return int(match[-1]) * MB


def targets():
    for app in ('idf_app', 'frame_app', 'rlcd_app', 'atom_app', 'halo_app', 'tough_app'):
        d = ROOT / app
        yield app, parse_table(d / 'partitions.csv'), flash_size([d / 'sdkconfig.defaults']), [d / 'sdkconfig.defaults']
    m5 = ROOT / 'm5_beta_app'
    for target in ('dial', 'sticks3', 'stopwatch', 'stackchan'):
        table = m5 / ('partitions.stackchan.csv' if target == 'stackchan' else 'partitions.csv')
        defaults = [m5 / 'sdkconfig.defaults', m5 / f'sdkconfig.{target}.defaults']
        yield f'm5_beta_app:{target}', parse_table(table), flash_size(defaults), defaults


class OtaLayoutTests(unittest.TestCase):
    def test_two_equal_update_slots_hold_the_published_image(self):
        for name, table, flash, _ in targets():
            with self.subTest(name):
                ota0, ota1 = table.get('ota_0'), table.get('ota_1')
                self.assertIsNotNone(ota0, 'ota_0 missing')
                self.assertIsNotNone(ota1, 'ota_1 missing')
                self.assertEqual(ota0[3], ota1[3], 'slots must be the same size')
                self.assertGreaterEqual(ota0[3], LARGEST_IMAGE[name] * HEADROOM)
                for slot in (ota0, ota1):
                    self.assertEqual(slot[2] % 0x10000, 0, 'app slots are 64 KB aligned')

    def test_partitions_fit_the_flash_and_never_overlap(self):
        for name, table, flash, _ in targets():
            with self.subTest(name):
                spans = sorted((o, o + s, n) for n, (_, _, o, s) in table.items())
                for (_, end, a), (start, _, b) in zip(spans, spans[1:]):
                    self.assertLessEqual(end, start, f'{a} overlaps {b}')
                self.assertLessEqual(spans[-1][1], flash)

    def test_settings_partition_stays_where_installed_devices_keep_it(self):
        for name, table, _, _ in targets():
            with self.subTest(name):
                self.assertEqual(table['nvs'][2:], NVS)
                self.assertIn('otadata', table)

    def test_a_bad_update_rolls_back(self):
        for name, _, _, defaults in targets():
            with self.subTest(name):
                text = '\n'.join(p.read_text() for p in defaults if p.exists())
                self.assertRegex(text, r'(?m)^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y')

    def test_every_app_builds_the_shared_client_and_confirms_its_boot(self):
        for app in ('idf_app', 'frame_app', 'rlcd_app', 'atom_app', 'halo_app', 'tough_app', 'm5_beta_app'):
            with self.subTest(app):
                cmake = (ROOT / app / 'main/CMakeLists.txt').read_text()
                self.assertIn('../../common/ota_update.c', cmake)
                self.assertRegex(cmake, r'app_update')


if __name__ == '__main__':
    unittest.main()
