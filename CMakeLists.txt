cmake_minimum_required(VERSION 3.3)

project(can-utils LANGUAGES C)

include (CheckSymbolExists)
include (GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

check_symbol_exists(__GNU_LIBRARY__ "features.h" _GNU_SOURCE)
if(_GNU_SOURCE)
  add_definitions(-D_GNU_SOURCE)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-parentheses")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSO_RXQ_OVFL=40")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPF_CAN=29")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAF_CAN=PF_CAN")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DN_SLCAN=17")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSCM_TIMESTAMPING_OPT_STATS=54")

include_directories (.)
include_directories (./include)

set(PROGRAMS_CANLIB
    asc2log
    canbusload
    candump
    cangen
    canlogserver
    canplayer
    cansend
    cansequence
    log2asc
    log2long
)

set(PROGRAMS_J1939
    j1939acd
    j1939cat
    j1939spy
    j1939sr
    testj1939
)

set(PROGRAMS
    ${PROGRAMS_CANLIB}
    bcmserver
    can-calc-bit-timing
    canfdtest
    cangw
    cansniffer
    isotpdump
    isotpperf
    isotprecv
    isotpsend
    isotpserver
    isotpsniffer
    isotptun
    slcan_attach
    slcand
    slcanpty
)

if(NOT ANDROID)
  list(APPEND PROGRAMS ${PROGRAMS_J1939})

  add_library(j1939 STATIC
      libj1939.c
  )
endif()

add_library(can STATIC
    lib.c
    canframelen.c
)

foreach(name ${PROGRAMS})
  add_executable(${name} ${name}.c)

  if("${name}" IN_LIST PROGRAMS_J1939)
    target_link_libraries(${name}
        PRIVATE j1939
    )
  elseif("${name}" IN_LIST PROGRAMS_CANLIB)
    target_link_libraries(${name}
        PRIVATE can
    )
  endif()

  install(TARGETS ${name} DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()


ADD_CUSTOM_TARGET(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/make_uninstall.cmake")
