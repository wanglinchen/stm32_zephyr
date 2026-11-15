# SPDX-License-Identifier: Apache-2.0
# 应用入口：Zephyr 通过 find_package(Zephyr) 注入内核、驱动与链接脚本
cmake_minimum_required(VERSION 3.20.0)

# 自定义板与 out-of-tree 驱动模块都在本仓库内，必须在 find_package(Zephyr) 之前登记。
# 这样 IDE 直接打开、裸 cmake 配置、以及 scripts/build.ps1 三种入口都能找到它们，
# 不再依赖命令行上的 -DBOARD_ROOT / -DZEPHYR_EXTRA_MODULES。
get_filename_component(app_project_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 裸 cmake / IDE 场景：Zephyr 的配置脚本需要 pyelftools、jsonschema、pykwalify 等包。
# scripts/env.ps1 把解释器放在环境变量 WEST_PYTHON 里，而 zephyr/cmake/modules/python.cmake
# 只在 CMake 变量 WEST_PYTHON / Python3_EXECUTABLE 已定义时才采用它（west build 会代转），
# 这里补上这一步，避免 CMake 自己搜到系统里缺包的解释器。
if(NOT DEFINED Python3_EXECUTABLE AND DEFINED ENV{WEST_PYTHON})
  file(TO_CMAKE_PATH "$ENV{WEST_PYTHON}" _west_python)
  if(EXISTS "${_west_python}")
    set(Python3_EXECUTABLE "${_west_python}")
  endif()
endif()

# 规范化已有条目（命令行或 CMakeCache 里可能是反斜杠路径），避免同一目录被判成两项
if(DEFINED BOARD_ROOT)
  set(_board_root_normalized "")
  foreach(_entry IN LISTS BOARD_ROOT)
    file(TO_CMAKE_PATH "${_entry}" _entry_norm)
    list(APPEND _board_root_normalized "${_entry_norm}")
  endforeach()
  set(BOARD_ROOT "${_board_root_normalized}")
else()
  set(BOARD_ROOT "")
endif()
list(FIND BOARD_ROOT "${app_project_root}" _board_root_index)
if(_board_root_index EQUAL -1)
  list(APPEND BOARD_ROOT "${app_project_root}")
endif()

if(DEFINED ZEPHYR_EXTRA_MODULES)
  set(_extra_modules_normalized "")
  foreach(_entry IN LISTS ZEPHYR_EXTRA_MODULES)
    file(TO_CMAKE_PATH "${_entry}" _entry_norm)
    list(APPEND _extra_modules_normalized "${_entry_norm}")
  endforeach()
  set(ZEPHYR_EXTRA_MODULES "${_extra_modules_normalized}")
else()
  set(ZEPHYR_EXTRA_MODULES "")
endif()
list(FIND ZEPHYR_EXTRA_MODULES "${app_project_root}/modules/apollo_f429" _module_index)
if(_module_index EQUAL -1)
  list(APPEND ZEPHYR_EXTRA_MODULES "${app_project_root}/modules/apollo_f429")
endif()

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(stm32f429_zephyr_app VERSION 0.1.0 LANGUAGES C)

# 应用层：每个模块一个源文件，头文件集中在 include/
# app_devices.c：`devices` shell 命令，用于板载模块自检
target_sources(app PRIVATE
    src/main.c
    src/app_led.c
    src/app_shell.c
    src/app_devices.c
)

target_include_directories(app PRIVATE include)
