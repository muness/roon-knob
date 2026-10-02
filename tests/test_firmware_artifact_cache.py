import importlib.util
from pathlib import Path
import unittest
import tempfile
import subprocess
import os
import json
spec=importlib.util.spec_from_file_location('cache',Path(__file__).parents[1]/'scripts/firmware_artifact_cache.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
class CacheTests(unittest.TestCase):
    def test_input_changes(self):
        base=[('common/a.c','1'),('idf_app/dependencies.lock','1'),('tests/test_a.c','1'),('web/flash.html','1'),('tough_app/main/a.c','1')]
        original=c.fingerprint(base,'dial','')
        for index in [0,1]:
            changed=base.copy();changed[index]=(base[index][0],'2');self.assertNotEqual(original,c.fingerprint(changed,'dial',''))
        for index in [2,3,4]:
            changed=base.copy();changed[index]=(base[index][0],'2');self.assertEqual(original,c.fingerprint(changed,'dial',''))
        self.assertNotEqual(c.fingerprint(base,'m5','dial'),c.fingerprint(base,'m5','stackchan'))
    def test_dependencies_fail_closed(self):
        for path in ['new-build-input','components/a.c','.github/workflows/docker.yml','scripts/audit_firmware_stack.py','tests/fixtures/rk_ble_hid_off/main.c']:
            self.assertTrue(c.relevant(path,'dial'))
        self.assertTrue(c.relevant('tough_app/main/a.c','m5'))
        self.assertTrue(c.relevant('frame_app/main/a.c','rlcd'))
    def test_workflow_gates_and_uploads(self):
        import yaml
        workflow=yaml.safe_load((Path(__file__).parents[1]/'.github/workflows/docker.yml').read_text())
        for name, job in workflow['jobs'].items():
            if not name.startswith('build-'): continue
            self.assertEqual(job['needs'], 'test-shared')
            steps=job['steps']
            restore=next(x for x in steps if x.get('id')=='firmware-cache')
            self.assertEqual(restore['if'], "github.event_name == 'pull_request'")
            self.assertNotIn('restore-keys',restore['with'])
            build=next(x for x in steps if x.get('uses','').startswith('espressif/esp-idf-ci-action'))
            self.assertEqual(build['if'], "steps.reuse.outputs.reused != 'true'")
            upload=next(x for x in steps if x.get('uses','').startswith('actions/upload-artifact'))
            self.assertTrue(upload['with']['path'].startswith('firmware-cache/'))

    def test_archive_verification_and_original_identity(self):
        script=Path(c.__file__).resolve()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            subprocess.run(['git','init','-q',tmp],check=True)
            (root/'common').mkdir();(root/'common/a.c').write_text('source')
            subprocess.run(['git','-C',tmp,'add','.'],check=True)
            subprocess.run(['git','-C',tmp,'-c','user.name=Test','-c','user.email=test@example.com','commit','-qm','fixture'],check=True)
            (root/'app.bin').write_bytes(b'firmware')
            env=dict(os.environ,FIRMWARE_FILES='app.bin',GITHUB_SHA='original-sha',GITHUB_RUN_ID='12',GITHUB_STEP_SUMMARY=str(root/'summary'))
            cmd=['python3',str(script),'stage','--target','dial','--directory','bundle']
            subprocess.run(cmd,cwd=tmp,env=env,check=True)
            env['GITHUB_SHA']='new-sha';cmd[2]='verify'
            subprocess.run(cmd,cwd=tmp,env=env,check=True)
            data=json.loads((root/'bundle/provenance-dial.json').read_text())
            self.assertEqual(data['build_sha'],'original-sha')
            (root/'bundle/app.bin').write_bytes(b'corrupt')
            self.assertNotEqual(subprocess.run(cmd,cwd=tmp,env=env,capture_output=True).returncode,0)
            (root/'bundle/app.bin').unlink()
            self.assertNotEqual(subprocess.run(cmd,cwd=tmp,env=env,capture_output=True).returncode,0)

if __name__=='__main__':unittest.main()
