cmake_minimum_required(VERSION 3.14.0)
project(powerpc VERSION 17.15)

include(cmake/bin2h.cmake)
include(cmake/verstring.cmake)
get_verstring(VERSTRING)
get_git_hash(GIT_HASH)

if(NOT TARGET devicetree)
    add_subdirectory(devicetree.resource EXCLUDE_FROM_ALL)
endif()

add_library(powerpc INTERFACE)
target_include_directories(powerpc INTERFACE include)

add_custom_target(build_ppc
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_SOURCE_DIR}/ppc
        -B ${CMAKE_BINARY_DIR}/ppc
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/toolchains/ppc-linux-gnu.cmake
    COMMAND ${CMAKE_COMMAND}
        --build ${CMAKE_BINARY_DIR}/ppc
    USES_TERMINAL
)

add_executable(powerpc.library
    src/main.c
    src/init.c
    src/support.c
    src/process.c
    src/interrupt.c
    src/msgframe.c
    src/asmstubs.c
)

add_dependencies(powerpc.library build_ppc)

bin_to_header(powerpc.library)

target_link_options(powerpc.library PRIVATE -m68040 -ffreestanding -nostdlib -nostartfiles  -Wl,-e_rom_start -Wl,-T${CMAKE_CURRENT_SOURCE_DIR}/ldscript.lds)
target_compile_options(powerpc.library PRIVATE -Os -m68040 -fomit-frame-pointer)
target_compile_definitions(powerpc.library PRIVATE 
    VERSION_STRING="${VERSTRING}"
    GIT_HASH=0x${GIT_HASH}
)
target_include_directories(powerpc.library PRIVATE include)
target_link_libraries(powerpc.library devicetree amiga)

set_target_properties(powerpc.library PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ldscript.lds)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/.git/index)
