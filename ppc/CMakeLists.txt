cmake_minimum_required(VERSION 3.14.0)
project(Emu68-PowerPC-ROM VERSION 0.1.0)
set(PROJECT_VERSION 0.1.0)

find_program(GZIP gzip REQUIRED)
find_program(XXD xxd REQUIRED)

include(../cmake/verstring.cmake)

set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)

add_executable(ppc_rom
  init.c
  kernel.c
  support.c
  patcher.c
  msgframe.c

  libfunc/addheadppc.c
  libfunc/addtailppc.c
  libfunc/addtimeppc.c
  libfunc/changemmu.c
  libfunc/cmptimeppc.c
  libfunc/copymemppc.c
  libfunc/enqueueppc.c
  libfunc/findnameppc.c
  libfunc/findtagitemppc.c
  libfunc/gettagdatappc.c
  libfunc/newlistppc.c
  libfunc/nexttagitemppc.c
  libfunc/subtimeppc.c
  libfunc/removeppc.c
  libfunc/remheadppc.c
  libfunc/remtailppc.c
  libfunc/sprintf.c
  libfunc/insertppc.c
  libfunc/getinfo.c
  libfunc/gethalinfo.c
  libfunc/super.c
  libfunc/user.c
  libfunc/causeinterrupt.c
)

get_verstring(VERSTRING)
get_git_hash(GIT_HASH)

target_link_options(ppc_rom PRIVATE -Wl,--build-id=none -nostdlib -nostartfiles -static -Wl,-Map -Wl,${CMAKE_BINARY_DIR}/ppc_rom.map -T ${LINKER_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR}/ldscript-ppc.lds)
target_compile_options(ppc_rom PRIVATE -fno-pic -fno-delete-null-pointer-checks -fpack-struct=2 -falign-functions=32 -O2 -mcpu=G3 -fno-asynchronous-unwind-tables -fno-dwarf2-cfi-asm -fno-strict-aliasing -fno-exceptions -ffreestanding -fcall-saved-r2 -fcall-saved-r12 -fcall-saved-r13 -Wall -Wextra -Werror)
target_include_directories(ppc_rom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR})
target_compile_definitions(ppc_rom PRIVATE __PPC__)

target_include_directories(ppc_rom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories(ppc_rom PRIVATE /opt/m68k-amigaos/m68k-amigaos/ndk-include)

add_custom_command(
    TARGET ppc_rom POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -v -O binary ppc_rom ppc_rom.img
    COMMAND ${XXD} -i ppc_rom.img >ppc_rom.h
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

configure_file(version.h.in version.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/.git/index)
