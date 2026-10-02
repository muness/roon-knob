#!/usr/bin/env python3
"""A refreshed page cannot silently load the previous build's CSS or JS."""

import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("assets", ROOT / "scripts/version_firmware_site_assets.py")
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class AssetVersioningTests(unittest.TestCase):
    def test_changed_bytes_change_urls_and_keep_old_assets_valid(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "assets/fonts").mkdir(parents=True)
            template = '<link href="./assets/styles.css"><script src="./assets/site.js"></script>'
            (root / "assets/styles.css").write_text('url("./fonts/local.woff2")')
            (root / "assets/site.js").write_text('old();')
            (root / "index.html").write_text(template)
            assets.version_assets(root)
            first = (root / "index.html").read_text()
            first_js = re.search(r'src="./(.*?)"', first)[1]
            self.assertEqual((root / first_js).read_text(), 'old();')
            self.assertIn('./fonts/local.woff2', next((root / "assets").glob('styles-*.css')).read_text())
            (root / "assets/site.js").write_text('new();')
            (root / "index.html").write_text(template)
            assets.version_assets(root)
            second = (root / "index.html").read_text()
            second_js = re.search(r'src="./(.*?)"', second)[1]
            self.assertNotEqual(first_js, second_js)
            self.assertEqual((root / first_js).read_text(), 'old();')
            self.assertEqual((root / second_js).read_text(), 'new();')
            self.assertEqual(re.search(r'href="(.*?)"', first)[1], re.search(r'href="(.*?)"', second)[1])
            self.assertNotIn('./assets/site.js', second)
            assets.version_assets(root)
            self.assertEqual((root / "index.html").read_text(), second)

    def test_missing_asset_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(FileNotFoundError):
                assets.version_assets(Path(temp))

    def test_all_publish_paths_version_and_verify_assets(self):
        workflow = (ROOT / '.github/workflows/docker.yml').read_text()
        for directory in ('pr-preview', 'gh-pages-build', 'publish-root'):
            self.assertIn(f'python3 scripts/version_firmware_site_assets.py {directory}', workflow)
        self.assertIn('find pr-preview -type f', workflow)
        self.assertIn('version_assets(output)', (ROOT / 'scripts/render_firmware_site_preview.py').read_text())


if __name__ == '__main__':
    unittest.main()
