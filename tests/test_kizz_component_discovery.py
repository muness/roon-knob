"""Execute the wake component CMake in early-discovery and registration phases.

IDF discovers excluded components before trimming the graph. The OFF and
unset cases must succeed without requesting source, model or runtime deps.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / 'components/kizz_wake_word/CMakeLists.txt'
fixture = r'''
cmake_minimum_required(VERSION 3.16)
set(COMPONENT_LIB discovery_component)
function(idf_build_get_property var property)
    if(DEFINED WAKE_OPTION)
        set(${var} "${WAKE_OPTION}" PARENT_SCOPE)
    else()
        unset(${var} PARENT_SCOPE)
    endif()
endfunction()
macro(idf_component_register)
    cmake_parse_arguments(REG "" "" "SRCS;INCLUDE_DIRS;REQUIRES;PRIV_REQUIRES" ${ARGN})
    file(WRITE "${REGISTRATION}" "${REG_SRCS}|${REG_INCLUDE_DIRS}|${REG_REQUIRES}|${REG_PRIV_REQUIRES}")
    # Match IDF's early-expansion macro: registration ends component discovery.
    if(EARLY)
        return()
    endif()
endmacro()
function(target_add_binary_data target asset kind)
    file(APPEND "${MODELS}" "${asset}\n")
endfunction()
function(set_property)
endfunction()
function(discover_component)
    include("${COMPONENT}")
endfunction()
discover_component()
'''
with tempfile.TemporaryDirectory(prefix='hiphi-kizz-cmake-') as directory:
    tmp = Path(directory)
    script = tmp / 'discovery.cmake'
    script.write_text(fixture)
    for early in ('ON', 'OFF'):
        for option in (None, 'OFF', 'ON'):
            registration = tmp / 'registration.txt'
            models = tmp / 'models.txt'
            registration.unlink(missing_ok=True)
            models.unlink(missing_ok=True)
            command = ['cmake', f'-DEARLY={early}', f'-DCOMPONENT={COMPONENT}',
                       f'-DREGISTRATION={registration}', f'-DMODELS={models}']
            if option is not None:
                command.append(f'-DWAKE_OPTION={option}')
            subprocess.run(command + ['-P', str(script)], text=True,
                           capture_output=True, check=True)
            value = registration.read_text()
            if option != 'ON':
                assert value == '|||', (early, option, value)
                assert not models.exists(), (early, option)
            else:
                assert 'kizz_wake_word.cpp' in value
                assert 'kizz_detector_aot.cpp' in value
                assert 'kizz_verifier_aot.cpp' in value
                assert 'micro_wake_word' in value
                assert 'espressif__esp-nn' in value
                if early == 'ON':
                    assert not models.exists()
                else:
                    assert len(models.read_text().splitlines()) == 3
print('Kizz CMake discovery passes: OFF/unset register empty; explicit ON retains runtime sources and models in both IDF phases.')
