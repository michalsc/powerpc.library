cmake_minimum_required(VERSION 3.14.0)
project(powerpc VERSION 17.15.0)

include(cmake/bin2h.cmake)
include(cmake/verstring.cmake)
get_verstring(VERSTRING)

if(NOT TARGET devicetree)
    add_subdirectory(devicetree.resource EXCLUDE_FROM_ALL)
endif()

add_library(powerpc INTERFACE)
target_include_directories(powerpc INTERFACE include)

add_executable(powerpc.library
    src/main.c
    src/init.c
)

bin_to_header(powerpc.library)

target_link_options(powerpc.library PRIVATE -m68040 -ffreestanding -nostdlib -nostartfiles -s -Wl,-e_rom_start -Wl,-T${CMAKE_CURRENT_SOURCE_DIR}/ldscript.lds)
target_compile_options(powerpc.library PRIVATE -Os -m68040 -fomit-frame-pointer)
target_compile_definitions(powerpc.library PRIVATE VERSION_STRING="${VERSTRING}")
target_include_directories(powerpc.library PRIVATE include)
target_link_libraries(powerpc.library devicetree)

set_target_properties(powerpc.library PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ldscript.lds)
