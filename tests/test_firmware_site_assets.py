#!/usr/bin/env python3
"""A refreshed page cannot silently load the previous build's CSS or JS."""

import importlib.util
from pathlib import Path
import re
import tempfile
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("assets", ROOT / "scripts/version_firmware_site_assets.py")
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


ALPHA_VERSION = re.search(r"^# Alpha v([^ ]+)", (ROOT / "web/assets/alpha-changelog.md").read_text())[1]

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

    def test_release_landing_page_verifies_rendered_asset_urls(self):
        import subprocess
        import shutil
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            shutil.copytree(ROOT / 'web/assets', root / 'assets')
            shutil.copyfile(ROOT / 'web/index.html', root / 'index.html')
            assets.version_assets(root)
            deployed = root / 'deployed.html'
            shutil.copyfile(root / 'index.html', deployed)
            workflow = (ROOT / '.github/workflows/docker.yml').read_text()
            comparison = re.search(r'cmp ([^\n]+) deployed-release/firmware-center.html', workflow)[1]
            expected = root / 'index.html' if comparison == 'publish-root/index.html' else ROOT / comparison
            self.assertEqual(subprocess.run(['cmp', str(expected), str(deployed)], capture_output=True).returncode, 0)
            # Raw source is deliberately different after rendering. This fails
            # the old workflow comparison even though the deployment is right.
            self.assertNotEqual((ROOT / 'web/index.html').read_bytes(), deployed.read_bytes())

    def comparison_fixture(self, root, channel="alpha", version=None):
        version = version or ALPHA_VERSION
        (root / 'assets').mkdir()
        for name in ('styles.css', 'site.js'):
            (root / 'assets' / name).write_text('asset')
        (root / 'assets/alpha-changelog.md').write_text((ROOT / 'web/assets/alpha-changelog.md').read_text())
        template = f'<body data-channel="{channel}"><p class="context-line">Alpha / v{version}</p><!-- alpha-comparison:start --><!-- alpha-comparison:end --></body>'
        (root / 'index.html').write_text(template)
        return template

    def test_alpha_comparison_is_static_safe_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.comparison_fixture(root)
            assets.prepare_site(root)
            text = (root / 'index.html').read_text()
            self.assertIn('Requires UHC v4', text)
            self.assertIn('Stable v2.5.2', text)
            self.assertIn('Library', text)
            self.assertIn('v4.0.0-beta.10', text)
            self.assertNotIn('**Dial', text)
            assets.prepare_site(root)
            self.assertEqual(text, (root / 'index.html').read_text())

    def test_stable_beta_and_unreleased_previews_do_not_borrow_alpha_claims(self):
        for channel, version in [('stable', '2.5.2'), ('beta', '2.8.0-beta.1'), ('alpha', 'pr-123'), ('alpha', '2.8.0-alpha.1-dev')]:
            with self.subTest(channel=channel, version=version), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                self.comparison_fixture(root, channel, version)
                (root / 'assets/alpha-changelog.md').unlink()
                assets.prepare_site(root)
                self.assertNotIn('Requires UHC', (root / 'index.html').read_text())

    def test_stale_notes_fail_before_any_page_is_modified(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            original = self.comparison_fixture(root)
            (root / 'next.html').write_text(original.replace('v' + ALPHA_VERSION, 'v99.0.0-alpha.1'))
            with self.assertRaisesRegex(ValueError, 'Update alpha-changelog'):
                assets.prepare_site(root)
            self.assertEqual(original, (root / 'index.html').read_text())

    def test_notes_escape_html_and_reject_incomplete_metadata(self):
        from render_firmware_site_preview import comparison
        source = (ROOT / 'web/assets/alpha-changelog.md').read_text()
        rendered = comparison(source.replace('artwork-only', '<script>bad()</script>'), ALPHA_VERSION)
        self.assertNotIn('<script>', rendered)
        self.assertIn('&lt;script&gt;', rendered)
        with self.assertRaises(ValueError):
            comparison(source.replace('## Requires UHC v4', '## Other'), ALPHA_VERSION)

    def test_preview_waits_for_matching_bytes_not_just_http_200(self):
        import os
        import subprocess
        import textwrap
        workflow = (ROOT / '.github/workflows/firmware-site-preview.yml').read_text()
        start = workflow.index('          PREVIEW_URL=')
        end = workflow.index('          while IFS=', start)
        check = textwrap.dedent(workflow[start:end])
        for fresh_after in (2, 100):
            with self.subTest(fresh_after=fresh_after), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / 'site-preview').mkdir()
                (root / 'site-preview/flash-frame.html').write_text('current')
                mock = root / 'bin'
                mock.mkdir()
                (mock / 'curl').write_text("#!/bin/sh\nn=$(cat count 2>/dev/null || echo 0)\nn=$((n+1))\necho $n > count\nif [ $n -ge $FRESH_AFTER ]; then printf current > deployed.html; else printf stale > deployed.html; fi\n")
                (mock / 'sleep').write_text('#!/bin/sh\nexit 0\n')
                for file in mock.iterdir():
                    file.chmod(0o755)
                env = dict(os.environ, PATH=str(mock)+os.pathsep+os.environ['PATH'], FRESH_AFTER=str(fresh_after), PR_NUM='1')
                result = subprocess.run(['bash', '-e', '-c', check], cwd=root, env=env, capture_output=True)
                self.assertEqual(result.returncode == 0, fresh_after == 2)
                if fresh_after == 2:
                    self.assertEqual((root / 'count').read_text().strip(), '2')

    def test_missing_asset_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(FileNotFoundError):
                assets.version_assets(Path(temp))

    def test_all_publish_paths_version_and_verify_assets(self):
        workflow = (ROOT / '.github/workflows/docker.yml').read_text()
        for directory in ('pr-preview', 'gh-pages-build', 'publish-root'):
            self.assertIn(f'python3 scripts/version_firmware_site_assets.py {directory}', workflow)
        self.assertIn('find pr-preview -type f', workflow)
        self.assertIn('prepare_site(output)', (ROOT / 'scripts/render_firmware_site_preview.py').read_text())


if __name__ == '__main__':
    unittest.main()
