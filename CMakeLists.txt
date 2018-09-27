cmake_minimum_required(VERSION 3.3)

project(can-utils LANGUAGES C)

include (GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  add_definitions(-D_GNU_SOURCE)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-parentheses")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-strict-aliasing")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSO_RXQ_OVFL=40")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DPF_CAN=29")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAF_CAN=PF_CAN")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DN_SLCAN=17")

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
    log2asc
    log2long
)

set(PROGRAMS_J1939
    jacd
    jspy
    jsr
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
endif()

foreach(name ${PROGRAMS})
  set(SRC_DEPS)
  if("${name}" STREQUAL "canbusload")
    set(SRC_DEPS canframelen.c)
  elseif("${name}" IN_LIST PROGRAMS_J1939)
    set(SRC_DEPS libj1939.c)
  elseif("${name}" IN_LIST PROGRAMS_CANLIB)
    set(SRC_DEPS lib.c)
  endif()

  add_executable(${name} ${name}.c ${SRC_DEPS})
  install(TARGETS ${name} DESTINATION ${CMAKE_INSTALL_BINDIR})
endforeach()


ADD_CUSTOM_TARGET(uninstall "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/cmake/make_uninstall.cmake")
