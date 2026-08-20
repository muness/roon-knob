cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.7.0-beta.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(hiphi_knob_aux_park)
