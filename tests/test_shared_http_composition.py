"""All ESP controller roots must use the same transport and explicit RAM policy."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
for target in ('idf_app', 'frame_app', 'rlcd_app', 'tough_app', 'atom_app', 'halo_app', 'm5_beta_app'):
    cmake = (root / target / 'main/CMakeLists.txt').read_text()
    sources = re.findall(r'"([^"\n]*platform_http[^"\n]*\.c)"', cmake)
    assert sources == ['../../common/platform/platform_http_esp.c'], (target, sources)
    fallback = target in ('tough_app', 'atom_app', 'halo_app', 'm5_beta_app')
    assert ('PLATFORM_HTTP_JSON_INTERNAL_FALLBACK=1' in cmake) == fallback, target
    assert ('PLATFORM_HTTP_IMAGE_REQUEST_GZIP=0' in cmake) == fallback, target
assert not list(root.glob('*_app/main/platform_http_*.c'))
print('shared HTTP target composition passed')
