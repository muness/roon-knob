import importlib.util
import pathlib
import tempfile
spec=importlib.util.spec_from_file_location('index','scripts/build_pr_index.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as d:
 root=pathlib.Path(d)
 notice='<div class="channel-notice alpha">Exact commit and run</div>'
 for target,run in [('dial',2),('frame',2),('tough',1)]:
  (root/f'flash-{target}-{"a"*40}-{run}-1.html').write_text(notice)
 out=m.build(root,250,'2026-09-13')
 assert 'flash-dial-' in out and 'flash-frame-' in out
 assert 'flash-tough-' not in out
 assert 'Exact commit and run' in out and '2026-09-13' in out
 (root/f'flash-joy-{"b"*40}-2-1.html').write_text(notice)
 try:m.build(root,250,'today')
 except ValueError:pass
 else:raise AssertionError('Mixed commit accepted')
print('PR chooser: latest generation, exact links, provenance and mixed-commit checks passed')
