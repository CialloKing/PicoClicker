cmake_minimum_required(VERSION 3.29)
project(PicoClicker VERSION 0.4.5 LANGUAGES C RC)

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
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64" AND CMAKE_LINKER_TYPE STREQUAL "MSVC")
        # The MSVC linker builds still use the CRT's full load config and its GuardMemcpy slot.
        set(guard_memcpy_source src/compact_memcpy.c)
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

option(PICOCLICKER_STANDARD_LOADER_COOKIE "Use the Windows loader's stack cookie in the standard targets" OFF)
set(lite_targets PicoClickerLite)
set(loader_cookie_targets)
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_LINKER_TYPE STREQUAL "LLD" AND
    CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x64)$")
    if(PICOCLICKER_STANDARD_LOADER_COOKIE)
        list(APPEND loader_cookie_targets PicoClicker PicoClickerLite)
    endif()
    find_program(PICOCLICKER_POWERSHELL NAMES pwsh powershell)
    if(PICOCLICKER_POWERSHELL)
        list(APPEND lite_targets PicoClickerExperimentalImage)
        list(APPEND loader_cookie_targets PicoClickerExperimentalImage)
    endif()
elseif(PICOCLICKER_STANDARD_LOADER_COOKIE)
    message(FATAL_ERROR "The standard loader cookie option requires x86 or x64 clang-cl and LLD.")
endif()
set(clicker_targets PicoClicker ${lite_targets})

foreach(clicker IN LISTS clicker_targets)
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
    if(NOT clicker STREQUAL "PicoClickerExperimentalImage")
        target_link_libraries(${clicker} PRIVATE kernel32 user32)
    endif()
    target_link_libraries(${clicker} PRIVATE libcmt libvcruntime)
    if(dos_stub)
        set_property(TARGET ${clicker} APPEND PROPERTY LINK_DEPENDS "${dos_stub}")
    endif()
endforeach()

if(guard_memcpy_source)
    target_sources(PicoClicker PRIVATE ${guard_memcpy_source})
    target_sources(PicoClickerLite PRIVATE ${guard_memcpy_source})
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_LINKER_TYPE STREQUAL "LLD")
    # Reserve a prefix the size of the resource tree in the editions where sharing a
    # read-only section saves a file block. Keep the original resources until packing.
    if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
        # The shorter PE32 load configuration leaves more room for relocations and resources.
        foreach(clicker IN LISTS clicker_targets)
            target_sources(${clicker} PRIVATE src/compact_load_config.c)
        endforeach()
        foreach(clicker IN LISTS lite_targets)
            target_compile_definitions(${clicker} PRIVATE PICO_RESOURCE_PREFIX=728)
            target_link_options(${clicker} PRIVATE /INCLUDE:_PicoResourcePrefix /MERGE:.rdata=.rpack)
        endforeach()
    elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        # In the full dialog, branching directly and loading imports at the call sites
        # saves registers and shortens the message loop. LLVM's private flags can vary by version.
        include(CheckCCompilerFlag)
        set(dialog_size_flags "/clang:-mllvm /clang:-disable-machine-licm /clang:-mllvm /clang:-x86-br-merging-base-cost=-1")
        check_c_compiler_flag("${dialog_size_flags}" PICO_HAVE_DIALOG_SIZE_FLAGS)
        if(PICO_HAVE_DIALOG_SIZE_FLAGS)
            target_compile_options(PicoClicker PRIVATE "$<$<COMPILE_LANGUAGE:C>:SHELL:${dialog_size_flags}>")
        endif()
        # Packing checks each map for references to guard slots past the compact CFG prefix.
        foreach(clicker IN LISTS clicker_targets)
            target_sources(${clicker} PRIVATE src/compact_load_config.c)
            target_link_options(${clicker} PRIVATE /INCLUDE:PicoResourcePrefix /MERGE:.rdata=.rpack /MAP)
        endforeach()
        target_compile_definitions(PicoClicker PRIVATE PICO_RESOURCE_PREFIX=792)
        foreach(clicker IN LISTS lite_targets)
            target_compile_definitions(${clicker} PRIVATE PICO_RESOURCE_PREFIX=728)
        endforeach()
    endif()
endif()

# The same definition controls both the code and the compiled dialog.
foreach(clicker IN LISTS lite_targets)
    target_compile_definitions(${clicker} PRIVATE PICOCLICKER_LITE)
endforeach()
set_target_properties(PicoClickerLite PROPERTIES OUTPUT_NAME PicoClicker-Lite)

# The loader replaces the cookie sentinel for the modern subsystem version.
foreach(clicker IN LISTS loader_cookie_targets)
    target_compile_definitions(${clicker} PRIVATE PICOCLICKER_LOADER_COOKIE)
    target_link_options(${clicker} PRIVATE /SUBSYSTEM:WINDOWS,6.03)
endforeach()

if(TARGET PicoClickerExperimentalImage)
    # Every target declares its libraries above. Omit CMake's implicit Win32
    # libraries so a missing entry in the experimental IAT is a link error.
    set(CMAKE_C_STANDARD_LIBRARIES "")
    set_target_properties(PicoClickerExperimentalImage PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        OUTPUT_NAME PicoClicker-Experimental-linked
    )
    get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    set(experimental_dir "${CMAKE_CURRENT_BINARY_DIR}")
    if(multi_config)
        string(APPEND experimental_dir "/$<CONFIG>")
    endif()

    if(CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "X86")
        set(import_triple i686-pc-windows-msvc)
    else()
        set(import_triple x86_64-pc-windows-msvc)
    endif()
    set(import_source "${CMAKE_CURRENT_SOURCE_DIR}/src/compact_imports.S")
    set(import_object "${experimental_dir}/PicoClicker-Imports.obj")
    add_custom_command(
        OUTPUT "${import_object}"
        BYPRODUCTS "${experimental_dir}/PicoClicker-Imports.asm"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${experimental_dir}"
        COMMAND "${CMAKE_C_COMPILER}" "--target=${import_triple}" /nologo /c
            "/Fo${import_object}" "/Fa${experimental_dir}/PicoClicker-Imports.asm" "${import_source}"
        DEPENDS "${import_source}"
        VERBATIM
    )
    target_sources(PicoClickerExperimentalImage PRIVATE "${import_object}")
    target_link_options(PicoClickerExperimentalImage PRIVATE /INCLUDE:PicoImportDescriptors)

    set(experimental_exe "${experimental_dir}/PicoClicker-Experimental.exe")
    set(experimental_packer "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compact-experimental-pe.ps1")
    add_custom_command(
        OUTPUT "${experimental_exe}"
        COMMAND "${PICOCLICKER_POWERSHELL}" -NoProfile -NonInteractive -File "${experimental_packer}"
            -Executable "$<TARGET_FILE:PicoClickerExperimentalImage>" -OutputFile "${experimental_exe}"
        DEPENDS PicoClickerExperimentalImage "${experimental_packer}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compact-pe.ps1"
        COMMENT "Packing the experimental PicoClicker PE"
        VERBATIM
    )
    add_custom_target(PicoClickerExperimental DEPENDS "${experimental_exe}")
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/release-info.txt"
    CONTENT "${PROJECT_VERSION}\n${toolchain}\n")
