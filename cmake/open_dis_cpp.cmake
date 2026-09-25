# open_dis_cpp.cmake — locate the installed open-dis-cpp DIS library and wrap
# it in an imported `open_dis_cpp` target, shared by backend (DIS input
# decode) and simulator (DIS emission). Included from both the top-level
# CMakeLists.txt and tools/simulator/CMakeLists.txt (standalone-configurable),
# hence the TARGET guard.
#
# open-dis-cpp (BSD-2-Clause, pinned v1.2.0) is NOT vendored in this repo: it
# is built from the pinned upstream release and installed into a prefix by
# scripts/install_open_dis.sh — into /usr/local inside the container build
# stage (containers/Containerfile.cpp), or wherever --prefix pointed for a
# host build. Headers are treated as SYSTEM so the auto-generated upstream
# code doesn't trip our -Wall/-Wpedantic in consumers.
#
# Search order: -DOLV_OPEN_DIS_PREFIX=DIR (if given), then /usr/local,
# /opt/open-dis, and ~/.local, plus the toolchain's standard paths.

if(TARGET open_dis_cpp)
  return()
endif()

set(OLV_OPEN_DIS_PREFIX "" CACHE PATH
    "Prefix where scripts/install_open_dis.sh installed open-dis-cpp")

set(_olv_open_dis_hints /usr/local /opt/open-dis "$ENV{HOME}/.local")
if(OLV_OPEN_DIS_PREFIX)
  list(PREPEND _olv_open_dis_hints "${OLV_OPEN_DIS_PREFIX}")
endif()

find_path(OLV_OPEN_DIS_INCLUDE_DIR dis6/EntityStatePdu.h
  HINTS ${_olv_open_dis_hints}
  PATH_SUFFIXES include)
find_library(OLV_OPEN_DIS_LIBRARY opendis6
  HINTS ${_olv_open_dis_hints}
  PATH_SUFFIXES lib)

if(NOT OLV_OPEN_DIS_INCLUDE_DIR OR NOT OLV_OPEN_DIS_LIBRARY)
  message(FATAL_ERROR
    "open-dis-cpp not found (looked for include/dis6/EntityStatePdu.h and "
    "lib/libopendis6.a under: ${_olv_open_dis_hints}). Install it with "
    "scripts/install_open_dis.sh [--prefix DIR], then reconfigure "
    "(optionally passing -DOLV_OPEN_DIS_PREFIX=DIR).")
endif()

add_library(open_dis_cpp STATIC IMPORTED)
set_target_properties(open_dis_cpp PROPERTIES
  IMPORTED_LOCATION "${OLV_OPEN_DIS_LIBRARY}")
target_include_directories(open_dis_cpp SYSTEM INTERFACE
  "${OLV_OPEN_DIS_INCLUDE_DIR}")

message(STATUS "open-dis-cpp: ${OLV_OPEN_DIS_LIBRARY}")
