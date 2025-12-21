cmake_minimum_required(VERSION 3.16)
project(roon_knob_pc C)

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.1.0
  GIT_SHALLOW TRUE
)
# cJSON requires old CMake policy - set minimum version to allow it
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_Declare(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.18
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl cjson)

find_package(CURL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

set(COMMON_SOURCES
  ../common/app_main.c
  ../common/roon_client.c
  ../common/ui.c
  ../common/controller_mode.c
  ../common/platform/platform_log.c
  ../common/platform/platform_time.c
  ../common/platform/platform_task.c
)

add_executable(roon_knob_pc
  main_pc.c
  ${COMMON_SOURCES}
  platform_http_pc.c
  platform_storage_pc.c
  platform_mdns_pc.c
  platform_input_pc.c
  components/net_client/curl_client.c
)

target_include_directories(roon_knob_pc PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/components/net_client
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${cjson_SOURCE_DIR}
)

target_compile_definitions(roon_knob_pc PRIVATE TARGET_PC=1 LV_CONF_INCLUDE_SIMPLE)

target_link_libraries(lvgl PUBLIC SDL2::SDL2)
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})

target_link_libraries(roon_knob_pc PRIVATE CURL::libcurl lvgl cjson SDL2::SDL2 Threads::Threads)

install(TARGETS roon_knob_pc DESTINATION bin)
