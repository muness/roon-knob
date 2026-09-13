import importlib.util
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('prune',Path(__file__).parents[1]/'scripts/prune_firmware_previews.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class RetentionTests(unittest.TestCase):
 def test_scope_latest_run_and_closed_pr(self):
  with tempfile.TemporaryDirectory() as d:
   r=Path(d)
   old='a'*40+'-100-1';new='b'*40+'-101-1'
   names=['stable/firmware.bin','alpha/firmware.bin','beta/index.html','pr/2/firmware.bin','pr/1/site/index.html','pr/1/assets/site.js',f'pr/1/dial-{old}.bin',f'pr/1/dial-{new}.bin',f'pr/1/frame-{new}.bin','pr/3/legacy.bin']
   for name in names:
    p=r/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('content')
   m.prune(r,{1,3},False);self.assertTrue((r/f'pr/1/dial-{old}.bin').exists())
   m.prune(r,{1,3},True)
   self.assertFalse((r/'pr/2').exists());self.assertFalse((r/f'pr/1/dial-{old}.bin').exists())
   for name in names:
    if name.startswith('pr/2/') or old in name:continue
    self.assertTrue((r/name).exists(),name)
 def test_replacement_is_scoped_to_pr_directories(self):
  import yaml
  root=Path(__file__).parents[1]
  for workflow in ['docker.yml','firmware-site-preview.yml']:
   data=yaml.safe_load((root/'.github/workflows'/workflow).read_text())
   for job in data['jobs'].values():
    for step in job.get('steps',[]):
     if not step.get('uses','').startswith('peaceiris/actions-gh-pages'):continue
     options=step['with']
     self.assertEqual(options['keep_files'],not options.get('destination_dir','').startswith('pr/'))
if __name__=='__main__':unittest.main()
