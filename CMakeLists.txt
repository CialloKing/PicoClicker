cmake_minimum_required(VERSION 3.29)
project(PicoClicker VERSION 0.4.3 LANGUAGES C RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "PicoClicker requires the Windows SDK and an MSVC-compatible compiler.")
endif()

set(clicker_sources
    src/clicker.c
    src/clicker.rc
)
set_property(SOURCE src/clicker.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/clicker.manifest")
set(compile_options /W4 /WX /utf-8 /GS /guard:cf)
set(link_options)

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    list(APPEND compile_options /O1 /Gw /GL)
    list(APPEND link_options /LTCG)
    set(toolchain "MSVC ${CMAKE_C_COMPILER_VERSION}")
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    list(APPEND compile_options /clang:-Oz)
    set(toolchain "clang-cl ${CMAKE_C_COMPILER_VERSION}")
    if(CMAKE_LINKER_TYPE STREQUAL "LLD")
        # Native x64 objects produced a smaller code section in the full and ThinLTO comparisons.
        if(NOT CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
            list(APPEND compile_options /clang:-flto)
        endif()
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
            # The clicker only needs scalar integer operations. Keep their smaller x86 encoding.
            list(APPEND compile_options /clang:-mno-sse /clang:-mno-sse2)
        endif()
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x64)$")
            # The 64-byte MZ image exits in DOS with code 1 (B8 01 4C CD 21 at offset 0x30).
            # It leaves room for the PE headers to fit in one 512-byte block after packing.
            set(dos_stub "${CMAKE_CURRENT_SOURCE_DIR}/src/x86-dos-stub.bin")
            list(APPEND link_options "/STUB:${dos_stub}")
        endif()
        string(APPEND toolchain " + LLD")
    elseif(CMAKE_LINKER_TYPE STREQUAL "MSVC")
        string(APPEND toolchain " + MSVC linker")
        if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
            # Omit the POGO section-group debug metadata when the linker accepts the switch.
            include(CheckLinkerFlag)
            check_linker_flag(C "/NOCOFFGRPINFO" PICO_HAVE_NOCOFFGRPINFO)
            if(PICO_HAVE_NOCOFFGRPINFO)
                list(APPEND link_options /NOCOFFGRPINFO)
            endif()
        endif()
    endif()
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64" AND CMAKE_LINKER_TYPE MATCHES "^(LLD|MSVC)$")
        # GuardMemcpy needs a copy target. Supply a small one instead of pulling in the CRT routine.
        list(APPEND clicker_sources src/compact_memcpy.c)
        set_property(SOURCE src/compact_memcpy.c APPEND PROPERTY COMPILE_OPTIONS /clang:-ffreestanding)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND (CMAKE_LINKER_TYPE STREQUAL "MSVC" OR
        (CMAKE_LINKER_TYPE STREQUAL "LLD" AND CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")))
        # Keep the unwind table in read-only data without a separate padding block.
        list(APPEND link_options "/MERGE:.pdata=.rdata")
    endif()
else()
    message(FATAL_ERROR "Use cl or clang-cl for the PicoClicker release build.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND link_options /SAFESEH)
endif()

# The resource embeds the manifest. Link only the CRT support reached from the custom entry.
list(APPEND link_options
    /MANIFEST:NO /ENTRY:PicoClickerEntry /NODEFAULTLIB
    /OPT:REF /OPT:ICF /INCREMENTAL:NO
    /DYNAMICBASE /NXCOMPAT /GUARD:CF
)

foreach(clicker IN ITEMS PicoClicker PicoClickerLite)
    add_executable(${clicker} WIN32 ${clicker_sources})
    set_target_properties(${clicker} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        MSVC_RUNTIME_LIBRARY MultiThreaded
    )
    target_compile_definitions(${clicker} PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    target_compile_options(${clicker} PRIVATE "$<$<COMPILE_LANGUAGE:C>:${compile_options}>")
    target_link_options(${clicker} PRIVATE ${link_options})
    target_link_libraries(${clicker} PRIVATE kernel32 user32 libcmt libvcruntime)
    if(dos_stub)
        set_property(TARGET ${clicker} APPEND PROPERTY LINK_DEPENDS "${dos_stub}")
    endif()
endforeach()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_LINKER_TYPE STREQUAL "LLD")
    # Reserve a prefix the size of the resource tree in the editions where sharing a
    # read-only section saves a file block. Keep the original resources until packing.
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
        # The shorter PE32 load configuration leaves more room for relocations and resources.
        target_sources(PicoClicker PRIVATE src/compact_load_config32.c)
        target_sources(PicoClickerLite PRIVATE src/compact_load_config32.c)
        target_compile_definitions(PicoClickerLite PRIVATE PICO_RESOURCE_PREFIX=728)
        target_link_options(PicoClickerLite PRIVATE /INCLUDE:_PicoResourcePrefix /MERGE:.rdata=.rpack)
    elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        target_compile_definitions(PicoClicker PRIVATE PICO_RESOURCE_PREFIX=792)
        target_link_options(PicoClicker PRIVATE /INCLUDE:PicoResourcePrefix /MERGE:.rdata=.rpack)
    endif()
endif()

# The same definition controls both the code and the compiled dialog.
target_compile_definitions(PicoClickerLite PRIVATE PICOCLICKER_LITE)
set_target_properties(PicoClickerLite PROPERTIES OUTPUT_NAME PicoClicker-Lite)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/release-info.txt"
    CONTENT "${PROJECT_VERSION}\n${toolchain}\n")
