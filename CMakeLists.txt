cmake_minimum_required(VERSION 3.29)
project(PicoClicker VERSION 0.1.0 LANGUAGES C RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "PicoClicker requires the Windows SDK and an MSVC-compatible compiler.")
endif()

add_executable(PicoClicker WIN32
    src/clicker.c
    src/clicker.rc
)
set_target_properties(PicoClicker PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    MSVC_RUNTIME_LIBRARY MultiThreaded
)

target_compile_definitions(PicoClicker PRIVATE WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(PicoClicker PRIVATE "$<$<COMPILE_LANGUAGE:C>:/W4;/WX;/utf-8>")

# The resource file embeds the application manifest.
target_link_options(PicoClicker PRIVATE /MANIFEST:NO)
target_link_libraries(PicoClicker PRIVATE kernel32 user32)
