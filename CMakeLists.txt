cmake_minimum_required(VERSION 3.5)

project(can-utils LANGUAGES C)

message(STATUS "CMake version: ${CMAKE_VERSION}")

include (CheckFunctionExists)
include (CheckSymbolExists)
include (GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Add an option to enable treating warnings as errors
option(ENABLE_WERROR "Treat all compiler warnings as errors" OFF)

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

include_directories (.)
include_directories (./include)

check_function_exists(fork HAVE_FORK)

set(PROGRAMS_CANLIB
  asc2log
  canbusload
  candump
  cangen
  canplayer
  cansend
  cansequence
  log2asc
  log2long
  slcanpty
)

if(HAVE_FORK)
  list(APPEND PROGRAMS_CANLIB canlogserver)
endif()

set(PROGRAMS_J1939
  j1939acd
  j1939cat
  j1939spy
  j1939sr
  testj1939
)

set(PROGRAMS_J1939_TIMEDATE
  j1939-timedate-srv
  j1939-timedate-cli
)

set(PROGRAMS_ISOBUSFS
  isobusfs-srv
  isobusfs-cli
)

set(PROGRAMS
  ${PROGRAMS_CANLIB}
  canfdtest
  cangw
  cansniffer
  isotpdump
  isotpperf
  isotprecv
  isotpsend
  isotpsniffer
  isotptun
  slcan_attach
  slcand
)

if(HAVE_FORK)
  list(APPEND PROGRAMS bcmserver)
  list(APPEND PROGRAMS isotpserver)
endif()

add_executable(can-calc-bit-timing
  calc-bit-timing/can-calc-bit-timing.c
)

add_executable(mcp251xfd-dump
  mcp251xfd/mcp251xfd-dev-coredump.c
  mcp251xfd/mcp251xfd-dump.c
  mcp251xfd/mcp251xfd-main.c
  mcp251xfd/mcp251xfd-regmap.c
)

if(NOT ANDROID)
  list(APPEND PROGRAMS ${PROGRAMS_J1939})

  add_library(j1939 STATIC
    libj1939.c
  )

  target_link_libraries(j1939
    PRIVATE can
  )

  add_library(isobusfs SHARED
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

  install(TARGETS
    isobusfs-cli
    isobusfs-srv
    DESTINATION ${CMAKE_INSTALL_BINDIR})

  set(PUBLIC_HEADER_j1939_TIMEDATE
    j1939_timedate/j1939_timedate_cmn.h
  )

  add_executable(j1939-timedate-cli
    j1939_timedate/j1939_timedate_cli.c
  )

  target_link_libraries(j1939-timedate-cli
    PRIVATE can j1939
  )

  add_executable(j1939-timedate-srv
    j1939_timedate/j1939_timedate_srv.c
  )

  target_link_libraries(j1939-timedate-srv
    PRIVATE can j1939
  )

  install(TARGETS
    j1939-timedate-cli
    j1939-timedate-srv
    DESTINATION ${CMAKE_INSTALL_BINDIR})

endif()

add_library(can STATIC
  lib.c
  canframelen.c
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

install(TARGETS
  can-calc-bit-timing
  mcp251xfd-dump
  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/make_uninstall.cmake"
  COMMENT "Add uninstall target"
)
