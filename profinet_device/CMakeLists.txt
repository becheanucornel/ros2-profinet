# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.14)
project(profinet_device VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(PNET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/p-net CACHE PATH "p-net v0.2.0")
set(OSAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/osal CACHE PATH "osal for p-net v0.2.0")

add_library(core STATIC src/modules.c src/state.c src/protocol.c)
target_include_directories(core PUBLIC src)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt)
  enable_testing()
  add_subdirectory(test)
endif()

# The rest needs Linux and p-net.
if (CMAKE_SYSTEM_NAME STREQUAL Linux)
  if (NOT EXISTS ${PNET_DIR}/CMakeLists.txt OR NOT EXISTS ${OSAL_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "Run profinet_device/third_party/fetch.sh first")
  endif()
  set(PNET_MAX_SLOTS 33 CACHE STRING "32 module slots and the device" FORCE)
  set(USE_SCHED_FIFO ON CACHE BOOL "real-time priority for the threads of p-net" FORCE)
  list(APPEND CMAKE_MODULE_PATH ${PNET_DIR}/cmake/tools)
  # p-net can be a subproject only without its own tests.
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  # osal first: p-net downloads it if the target does not exist yet.
  add_subdirectory(${OSAL_DIR} osal EXCLUDE_FROM_ALL)
  add_subdirectory(${PNET_DIR} p-net EXCLUDE_FROM_ALL)
  # Warnings of newer compilers must not break a frozen release.
  target_compile_options(osal PRIVATE -Wno-error)
  target_compile_options(profinet PRIVATE -Wno-error)

  find_package(Threads REQUIRED)
  add_executable(profinet_device src/main.c src/device.c)
  target_compile_options(profinet_device PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_include_directories(profinet_device PRIVATE ${PNET_DIR}/src/ports/linux)
  target_link_libraries(profinet_device core profinet Threads::Threads)

  # p-net runs this script from the directory of the binary.
  install(TARGETS profinet_device DESTINATION ${CMAKE_INSTALL_LIBDIR}/profinet_device)
  install(PROGRAMS ${PNET_DIR}/src/ports/linux/set_network_parameters
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/profinet_device)
  configure_file(systemd/profinet_device.service.in profinet_device.service @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/profinet_device.service DESTINATION lib/systemd/system)
endif()
