cmake_minimum_required(VERSION 3.29)
project(PicoClicker VERSION 0.2.0 LANGUAGES C RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "PicoClicker requires the Windows SDK and an MSVC-compatible compiler.")
endif()

add_executable(PicoClicker WIN32
    src/clicker.c
    src/clicker.rc
)
set_property(SOURCE src/clicker.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/clicker.manifest")
set_target_properties(PicoClicker PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    MSVC_RUNTIME_LIBRARY MultiThreaded
)

target_compile_definitions(PicoClicker PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(PicoClicker PRIVATE "$<$<COMPILE_LANGUAGE:C>:/W4;/WX;/utf-8;/GS;/guard:cf>")

if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(PicoClicker PRIVATE "$<$<COMPILE_LANGUAGE:C>:/O1;/Gw;/GL>")
    target_link_options(PicoClicker PRIVATE /LTCG)
    set(toolchain "MSVC ${CMAKE_C_COMPILER_VERSION}")
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(PicoClicker PRIVATE "$<$<COMPILE_LANGUAGE:C>:/clang:-Oz>")
    set(toolchain "clang-cl ${CMAKE_C_COMPILER_VERSION}")
    if(CMAKE_LINKER_TYPE STREQUAL "LLD")
        target_compile_options(PicoClicker PRIVATE "$<$<COMPILE_LANGUAGE:C>:/clang:-flto>")
        string(APPEND toolchain " + LLD")
    elseif(CMAKE_LINKER_TYPE STREQUAL "MSVC")
        string(APPEND toolchain " + MSVC linker")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            # Both sections are read-only. Keep the x64 unwind table without a separate padding block.
            target_link_options(PicoClicker PRIVATE "/MERGE:.pdata=.rdata")
        endif()
    endif()
else()
    message(FATAL_ERROR "Use cl or clang-cl for the PicoClicker release build.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(PicoClicker PRIVATE /SAFESEH)
endif()

# The resource embeds the manifest. Link only the CRT support reached from the custom entry.
target_link_options(PicoClicker PRIVATE
    /MANIFEST:NO /ENTRY:PicoClickerEntry /NODEFAULTLIB
    /OPT:REF /OPT:ICF /INCREMENTAL:NO
    /DYNAMICBASE /NXCOMPAT /GUARD:CF
)
target_link_libraries(PicoClicker PRIVATE kernel32 user32 libcmt libvcruntime)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/release-info.txt"
    CONTENT "${PROJECT_VERSION}\n${toolchain}\n")
