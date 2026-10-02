#!/usr/bin/env python3
"""Reject stale ON configurations, hidden models, and dependency graph leaks."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wake_gate', ROOT / 'scripts/check_kizz_disabled_build.py')
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class DisabledWakeBuildTests(unittest.TestCase):
    def test_arduino_adapter_selection_and_empty_registration(self):
        # Execute the actual project and adapter CMake rather than checking
        # spelling. IDF's project setup is stubbed; component resolution and
        # compilation are additionally checked by the real CI build gate.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            idf_cmake = root / 'idf/tools/cmake'
            idf_cmake.mkdir(parents=True)
            (idf_cmake / 'project.cmake').write_text('')
            fixture = root / 'check.cmake'
            fixture.write_text(r'''
cmake_minimum_required(VERSION 3.16)
set(ENV{IDF_PATH} "${FAKE_IDF}")
set(HIPHI_M5_TARGET stackchan CACHE STRING "")
function(idf_build_set_property name value)
    set_property(GLOBAL PROPERTY "fake_${name}" "${value}")
endfunction()
function(idf_build_get_property output name)
    get_property(value GLOBAL PROPERTY "fake_${name}")
    set(${output} "${value}" PARENT_SCOPE)
endfunction()
macro(project)
endmacro()
function(idf_component_register)
    if(ARGC GREATER 0)
        message(FATAL_ERROR "Disabled adapter requested build inputs: ${ARGV}")
    endif()
    set_property(GLOBAL PROPERTY adapter_registered ON)
endfunction()
include("${REPO}/m5_beta_app/CMakeLists.txt")
idf_build_get_property(DEPENDENCIES_LOCK DEPENDENCIES_LOCK)
if(NOT "$ENV{HIPHI_M5_PROJECT_DIR}" STREQUAL "${REPO}/m5_beta_app")
    message(FATAL_ERROR "Portable dependency lock anchor was not initialized")
endif()
if(HIPHI_M5_TARGET STREQUAL "sticks3" OR HIPHI_M5_TARGET STREQUAL "stopwatch")
    set(expected_lock "${REPO}/m5_beta_app/dependencies.native.lock")
else()
    set(expected_lock "${REPO}/m5_beta_app/dependencies.lock")
endif()
if(NOT DEPENDENCIES_LOCK STREQUAL expected_lock)
    message(FATAL_ERROR "Wrong dependency lock for ${HIPHI_M5_TARGET}: ${DEPENDENCIES_LOCK}")
endif()
set(adapter "${REPO}/m5_beta_app/../optional_components/voice_disabled/espressif__esp-sr")
if(HIPHI_KIZZ_WAKE_WORD)
    if(adapter IN_LIST EXTRA_COMPONENT_DIRS)
        message(FATAL_ERROR "Wake ON selected the disabled adapter")
    endif()
    if(FORCE_ADAPTER)
        include("${adapter}/CMakeLists.txt")
    endif()
else()
    if(NOT adapter IN_LIST EXTRA_COMPONENT_DIRS)
        message(FATAL_ERROR "Wake OFF failed to override Arduino's dependency")
    endif()
    if("espressif__esp-sr" IN_LIST EXCLUDE_COMPONENTS)
        message(FATAL_ERROR "EXCLUDE_COMPONENTS would hide the empty adapter from IDF's dependency resolver")
    endif()
    if(NOT EXISTS "${adapter}/idf_component.yml")
        message(FATAL_ERROR "The component manager ignores local overrides without a manifest")
    endif()
    include("${adapter}/CMakeLists.txt")
    get_property(registered GLOBAL PROPERTY adapter_registered)
    if(NOT registered)
        message(FATAL_ERROR "Disabled adapter did not register its empty interface")
    endif()
endif()
''')
            cases = [(target, 'OFF', False, True) for target in ('dial', 'sticks3', 'stopwatch', 'stackchan')]
            cases += [('stackchan', 'ON', False, True), ('stackchan', 'ON', True, False)]
            for target, option, force, succeeds in cases:
                with self.subTest(target=target, option=option, force=force):
                    result = subprocess.run(['cmake', f'-DREPO={ROOT}', f'-DFAKE_IDF={root / "idf"}',
                                             f'-DHIPHI_M5_TARGET={target}',
                                             f'-DHIPHI_KIZZ_WAKE_WORD={option}', f'-DFORCE_ADAPTER={"ON" if force else "OFF"}',
                                             '-P', str(fixture)], text=True, capture_output=True)
                    self.assertEqual(result.returncode == 0, succeeds, result.stderr)
                    if not succeeds:
                        self.assertIn('disabled ESP-SR adapter must not be used with wake ON', result.stderr)

    def test_rejects_real_arduino_transitive_source_leak(self):
        # The failed Dial Lab CI build compiled these ESP-SR sources despite
        # selective Arduino ESP_SR=n and EXCLUDE_COMPONENTS. Each is a leak
        # even without a wake model or a referenced symbol in the final image.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'hiphi_m5dial.map').write_text('m5_platform_voice_get_phase .text')
            platform = {'file': 'm5_platform.cpp', 'arguments': ['c++', '-DHIPHI_KIZZ_WAKE_WORD=0']}
            for source in ('esp_sr_debug.c', 'esp_process_sdkconfig.c', 'model_path.c', 'esp_mn_speech_commands.c'):
                with self.subTest(source=source):
                    entries = [platform, {'file': f'/repo/m5_beta_app/managed_components/espressif__esp-sr/src/{source}',
                                          'command': f'cc -c {source}'}]
                    (root / 'compile_commands.json').write_text(json.dumps(entries))
                    with self.assertRaisesRegex(ValueError, 'espressif__esp-sr'):
                        gate.check_build(root, 'hiphi_m5dial')

    def test_rejects_compiler_and_linker_leaks_independently(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            commands = [{'file': f'/repo/{source}',
                         'command': f'c++ -DHIPHI_KIZZ_WAKE_WORD=0 -c {source}'}
                        for source in gate.GATED_SOURCES]
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
            for source in gate.GATED_SOURCES:
                remaining = [entry for entry in commands if Path(entry['file']).name != source]
                write(remaining)
                with self.assertRaisesRegex(ValueError, source): gate.check_build(root, 'hiphi_stackchan')
                for flag in ('', '-DHIPHI_KIZZ_WAKE_WORD=1', '-DHIPHI_KIZZ_WAKE_WORD=0 -DHIPHI_KIZZ_WAKE_WORD=1'):
                    write(remaining + [{'file': source, 'command': 'c++ ' + flag}])
                    with self.assertRaisesRegex(ValueError, source): gate.check_build(root, 'hiphi_stackchan')
            write([])
            with self.assertRaises(ValueError): gate.check_build(root, 'hiphi_stackchan')
            write()
            (root / 'hiphi_stackchan.map').unlink()
            with self.assertRaises(FileNotFoundError): gate.check_build(root, 'hiphi_stackchan')


if __name__ == '__main__':
    unittest.main()
