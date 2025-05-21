cmake_minimum_required(VERSION 3.5)

project(can-utils LANGUAGES C)

message(STATUS "CMake version: ${CMAKE_VERSION}")

include(CheckFunctionExists)
include(CheckSymbolExists)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS STREQUAL "")
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "project default" FORCE)
endif()

# Add an option to enable treating warnings as errors
option(ENABLE_WERROR "Treat all compiler warnings as errors" OFF)
option(ENABLE_GPS "Enable GPS support" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Options for controlling targets. These groups correspond to the README.
option(ENABLE_BASIC_TOOLS "Build basic tools" ON)
option(ENABLE_IP_SERVER   "Build tools providing CAN access via IP sockets" ON)
option(ENABLE_GATEWAY     "Build in-kernel gateway configuration tools" ON)
option(ENABLE_MEASUREMENT "Build measurement tools" ON)
option(ENABLE_ISOTP       "Build isotp tools" ON)
option(ENABLE_LOG_FILE    "Build log file converter tools" ON)
option(ENABLE_SLCAN       "Build slcan tools" ON)
option(ENABLE_MCP251XFD   "Build MCP251XFD tools" ON)

# cmake_dependent_option is only available in CMake 3.22 and later.
if(ANDROID)
  set(ENABLE_J1939    OFF)
  set(ENABLE_ISOBUSFS OFF)
else()
  option(ENABLE_J1939    "Build J1939 tools"  ON)
  option(ENABLE_ISOBUSFS "Build ISOBUS tools" ON)
endif()

find_package(PkgConfig REQUIRED)
if(ENABLE_GPS)
  pkg_check_modules(GPS REQUIRED libgps)
endif()

if(ENABLE_WERROR)
  add_compile_options(-Werror)
endif()

add_definitions(-D_GNU_SOURCE)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-parentheses -Wsign-compare")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSO_RXQ_OVFL=40")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPF_CAN=29")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAF_CAN=PF_CAN")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DN_SLCAN=17")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSCM_TIMESTAMPING_OPT_STATS=54")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCLOCK_TAI=11")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSO_TXTIME=61")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSCM_TXTIME=SO_TXTIME")

include_directories(.)
include_directories(./include)

check_function_exists(fork HAVE_FORK)

# List of all programs to be built.
# Each program is expected to have a corresponding source file with the same name.
set(PROGRAMS)
# List of programs to link against can.
set(PROGRAMS_CANLIB)
# List of programs to link against j1939 and can.
set(PROGRAMS_J1939)

if(ENABLE_BASIC_TOOLS)
  list(APPEND PROGRAMS cansniffer)
  list(APPEND PROGRAMS_CANLIB
    candump
    canplayer
    cansend
    cangen
    cansequence
  )
endif()

if(ENABLE_IP_SERVER AND HAVE_FORK)
  list(APPEND PROGRAMS        bcmserver)
  list(APPEND PROGRAMS_CANLIB canlogserver)
endif()

if(ENABLE_GATEWAY)
  list(APPEND PROGRAMS cangw)
endif()

if(ENABLE_MEASUREMENT)
  list(APPEND PROGRAMS
    canfdtest
    canerrsim
  )
  list(APPEND PROGRAMS_CANLIB canbusload)

  add_executable(can-calc-bit-timing
    calc-bit-timing/can-calc-bit-timing.c
  )
  install(TARGETS can-calc-bit-timing DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(ENABLE_ISOTP)
  list(APPEND PROGRAMS
    isotpdump
    isotpperf
    isotprecv
    isotpsend
    isotpsniffer
    isotptun
  )

  if(HAVE_FORK)
    list(APPEND PROGRAMS isotpserver)
  endif()
endif()

if(ENABLE_J1939)
  list(APPEND PROGRAMS_J1939
    j1939acd
    j1939cat
    j1939spy
    j1939sr
    testj1939
  )

  add_executable(j1939-timedate-cli
    j1939_timedate/j1939_timedate_cli.c
  )
  target_link_libraries(j1939-timedate-cli
    PRIVATE can j1939
  )
  install(TARGETS j1939-timedate-cli DESTINATION ${CMAKE_INSTALL_BINDIR})

  add_executable(j1939-timedate-srv
    j1939_timedate/j1939_timedate_srv.c
  )
  target_link_libraries(j1939-timedate-srv
    PRIVATE can j1939
  )
  install(TARGETS j1939-timedate-srv DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(ENABLE_J1939 AND ENABLE_GPS)
  add_executable(j1939-vehicle-position-srv
    j1939_vehicle_position/j1939_vehicle_position_srv.c
  )
  target_link_libraries(j1939-vehicle-position-srv
    PRIVATE can j1939 ${GPS_LIBRARIES}
  )
  install(TARGETS j1939-vehicle-position-srv DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(ENABLE_ISOBUSFS)
  add_library(isobusfs EXCLUDE_FROM_ALL
    isobusfs/isobusfs_cmn.c
    isobusfs/isobusfs_cmn_dh.c
  )
  set(PUBLIC_HEADER_ISOBUSFS
    isobusfs/isobusfs_cmn.h
    isobusfs/isobusfs_cmn_cm.h
  )
  set_target_properties(isobusfs PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADER_ISOBUSFS}"
    SOVERSION 0
  )
  install(TARGETS isobusfs
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  add_executable(isobusfs-cli
    isobusfs/isobusfs_cli.c
    isobusfs/isobusfs_cli_cm.c
    isobusfs/isobusfs_cli_dh.c
    isobusfs/isobusfs_cli_fa.c
    isobusfs/isobusfs_cli_selftests.c
    isobusfs/isobusfs_cli_int.c
  )
  target_link_libraries(isobusfs-cli
    PRIVATE isobusfs can j1939
  )
  install(TARGETS isobusfs-cli DESTINATION ${CMAKE_INSTALL_BINDIR})

  add_executable(isobusfs-srv
    isobusfs/isobusfs_srv.c
    isobusfs/isobusfs_srv_cm.c
    isobusfs/isobusfs_srv_cm_fss.c
    isobusfs/isobusfs_srv_dh.c
    isobusfs/isobusfs_srv_fa.c
    isobusfs/isobusfs_srv_fh.c
    isobusfs/isobusfs_srv_vh.c
  )
  target_link_libraries(isobusfs-srv
    PRIVATE isobusfs can j1939
  )
  install(TARGETS isobusfs-srv DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(ENABLE_LOG_FILE)
  list(APPEND PROGRAMS_CANLIB
    asc2log
    log2asc
    log2long
  )
endif()

if(ENABLE_SLCAN)
  list(APPEND PROGRAMS
    slcan_attach
    slcand
  )
  list(APPEND PROGRAMS_CANLIB slcanpty)
endif()

if(ENABLE_MCP251XFD)
  add_executable(mcp251xfd-dump
    mcp251xfd/mcp251xfd-dev-coredump.c
    mcp251xfd/mcp251xfd-dump.c
    mcp251xfd/mcp251xfd-main.c
    mcp251xfd/mcp251xfd-regmap.c
  )
  install(TARGETS mcp251xfd-dump DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

list(APPEND PROGRAMS
  ${PROGRAMS_CANLIB}
  ${PROGRAMS_J1939}
)

add_library(can STATIC EXCLUDE_FROM_ALL
  lib.c
  canframelen.c
)

add_library(j1939 STATIC EXCLUDE_FROM_ALL
  libj1939.c
)
target_link_libraries(j1939
  PRIVATE can
)

foreach(name ${PROGRAMS})
  add_executable(${name} ${name}.c)

  if("${name}" IN_LIST PROGRAMS_J1939)
    target_link_libraries(${name}
      PRIVATE j1939 can
    )
  elseif("${name}" IN_LIST PROGRAMS_CANLIB)
    target_link_libraries(${name}
      PRIVATE can
    )
  endif()

  install(TARGETS ${name} DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/make_uninstall.cmake"
  COMMENT "Add uninstall target"
)
