cmake_minimum_required(VERSION 3.15)
project(xiaopengkernel VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_EXTENSIONS OFF)

# Platform detection
if(WIN32)
    set(MINGW_BUILD TRUE)
else()
    set(MINGW_BUILD FALSE)
endif()

# Copy MinGW runtime DLLs next to the executables: the exe directory wins the
# Windows DLL search order, so tests run even when an older toolchain's
# libstdc++-6.dll shadows ours on PATH (0xc0000139 entry-point failures).
if(MINGW_BUILD)
    get_filename_component(XPK_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(XPK_RUNTIME_DLL libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${XPK_MINGW_BIN}/${XPK_RUNTIME_DLL}")
            file(COPY "${XPK_MINGW_BIN}/${XPK_RUNTIME_DLL}" DESTINATION "${CMAKE_SOURCE_DIR}/bin")
        endif()
    endforeach()
endif()

# CURL Configuration - support both MinGW bundled and Linux system library
if(MINGW_BUILD)
    set(CURL_ROOT "${CMAKE_SOURCE_DIR}/third_party/curl")
    set(CURL_INCLUDE_DIR "${CURL_ROOT}/include")
    set(CURL_LIBRARY "${CURL_ROOT}/lib/libcurl.a")
else()
    find_package(CURL REQUIRED)
    set(CURL_INCLUDE_DIR ${CURL_INCLUDE_DIRS})
    set(CURL_LIBRARY CURL::libcurl)
endif()

# SDL2 Configuration
if(MINGW_BUILD)
    set(SDL2_ROOT "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.28.5/x86_64-w64-mingw32")
    if(EXISTS "${SDL2_ROOT}")
        set(SDL2_INCLUDE_DIR "${SDL2_ROOT}/include/SDL2")
        set(SDL2_LIBRARY "${SDL2_ROOT}/lib/libSDL2.a")
        set(SDL2_MAIN_LIBRARY "${SDL2_ROOT}/lib/libSDL2main.a")
        
        include_directories(${SDL2_INCLUDE_DIR})
        add_compile_definitions(ENABLE_SDL)
        message(STATUS "SDL2 found at ${SDL2_ROOT}")
        set(XPK_SDL_LINK_LIBRARIES mingw32 ${SDL2_MAIN_LIBRARY} ${SDL2_LIBRARY} winmm gdi32 imm32 ole32 oleaut32 version uuid advapi32 setupapi shell32 user32)
    else()
        message(WARNING "SDL2 not found at ${SDL2_ROOT}. Window support will be disabled.")
    endif()
else()
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        include_directories(${SDL2_INCLUDE_DIRS})
        add_compile_definitions(ENABLE_SDL)
        message(STATUS "SDL2 found (system): ${SDL2_INCLUDE_DIRS}")
        set(XPK_SDL_LINK_LIBRARIES ${SDL2_LIBRARIES})
    else()
        message(WARNING "SDL2 not found. Window support will be disabled.")
    endif()
endif()

# QuickJS Configuration
set(QUICKJS_ROOT "${CMAKE_SOURCE_DIR}/third_party/quickjs")
if(EXISTS "${QUICKJS_ROOT}/quickjs.c")
    set(QUICKJS_SOURCES
        "${QUICKJS_ROOT}/quickjs.c"
        "${QUICKJS_ROOT}/libregexp.c"
        "${QUICKJS_ROOT}/libunicode.c"
        "${QUICKJS_ROOT}/cutils.c"
        "${QUICKJS_ROOT}/quickjs-libc.c"
        "${QUICKJS_ROOT}/dtoa.c"
    )
    
    # QuickJS is C, so we need to ensure C compiler is available but project handles C/CXX.
    # It often requires specific defines.
    add_library(quickjs STATIC ${QUICKJS_SOURCES})
    target_include_directories(quickjs PUBLIC "${QUICKJS_ROOT}")
    target_compile_definitions(quickjs PRIVATE 
        CONFIG_VERSION="2021-03-27"
        _GNU_SOURCE 
    )
    set_target_properties(quickjs PROPERTIES LINKER_LANGUAGE C)
    # Suppress warnings for third-party C code
    if(MSVC)
        target_compile_options(quickjs PRIVATE /w)
    else()
        target_compile_options(quickjs PRIVATE -w)
    endif()

    add_compile_definitions(ENABLE_QUICKJS)
    message(STATUS "QuickJS found at ${QUICKJS_ROOT}")
else()
    message(WARNING "QuickJS not found at ${QUICKJS_ROOT}. Scripting will be disabled.")
endif()


# FreeType and HarfBuzz support (bundled in third_party)
if(ENABLE_FREETYPE)
    if(MINGW_BUILD)
        set(FREETYPE_ROOT "${CMAKE_SOURCE_DIR}/third_party/freetype")
        set(HARFBUZZ_ROOT "${CMAKE_SOURCE_DIR}/third_party/harfbuzz")
        if(EXISTS "${FREETYPE_ROOT}/lib/libfreetype.a" AND EXISTS "${HARFBUZZ_ROOT}/lib/libharfbuzz.a")
            add_compile_definitions(ENABLE_FREETYPE)
            include_directories(
                "${FREETYPE_ROOT}/include/freetype2"
                "${HARFBUZZ_ROOT}/include/harfbuzz"
            )
            set(FREETYPE_LIBRARIES
                "${HARFBUZZ_ROOT}/lib/libharfbuzz.a"
                "${FREETYPE_ROOT}/lib/libfreetype.a"
            )
            message(STATUS "FreeType and HarfBuzz enabled (bundled)")
        else()
            message(WARNING "ENABLE_FREETYPE is ON but libraries not found in third_party/. Disabling.")
            set(ENABLE_FREETYPE OFF)
        endif()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FREETYPE freetype2)
        pkg_check_modules(HARFBUZZ harfbuzz)
        if(FREETYPE_FOUND AND HARFBUZZ_FOUND)
            add_compile_definitions(ENABLE_FREETYPE)
            include_directories(${FREETYPE_INCLUDE_DIRS} ${HARFBUZZ_INCLUDE_DIRS})
            # combine the two libraries for target_link_libraries
            set(FREETYPE_LIBRARIES ${HARFBUZZ_LIBRARIES} ${FREETYPE_LIBRARIES})
            message(STATUS "FreeType and HarfBuzz enabled (system)")
        else()
            message(WARNING "ENABLE_FREETYPE is ON but system libraries not found. Disabling.")
            set(ENABLE_FREETYPE OFF)
        endif()
    endif()
endif()

# OpenGL Configuration (GPU accelerated rendering)
find_package(OpenGL)
if(OPENGL_FOUND)
    add_compile_definitions(ENABLE_OPENGL)
    message(STATUS "OpenGL found — GPU accelerated rendering enabled")
    # Add OpenGL include directories
    include_directories(${OPENGL_INCLUDE_DIR})
    if(MINGW_BUILD)
        set(XPK_OPENGL_LIBRARIES opengl32)
    elseif(TARGET OpenGL::GL)
        set(XPK_OPENGL_LIBRARIES OpenGL::GL)
    else()
        set(XPK_OPENGL_LIBRARIES ${OPENGL_LIBRARIES})
    endif()
else()
    message(WARNING "OpenGL not found. GPU rendering will be disabled (Software fallback only).")
endif()

include_directories(include)
include_directories(${CURL_INCLUDE_DIR})

# Collect all source files
file(GLOB_RECURSE ALL_SOURCES "src/*.cpp")
# Exclude standalone demos and specific mains from the core source list
list(FILTER ALL_SOURCES EXCLUDE REGEX "simple_opengl_demo\\.cpp$")
list(FILTER ALL_SOURCES EXCLUDE REGEX "demo_minimal_main\\.cpp$")
list(FILTER ALL_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
# Exclude the legacy QuickJS Promise shim from the core build. The project uses
# promise_binding_simple.cpp for the current event-loop integration, and building
# both implementations defines the same PromiseBinding symbols.
list(FILTER ALL_SOURCES EXCLUDE REGEX "promise_binding\\.cpp$")

# Create an OBJECT library for the core framework to be reused by multiple targets
add_library(xiaopengkernel_core OBJECT ${ALL_SOURCES})
target_compile_definitions(xiaopengkernel_core PRIVATE 
    _CRT_SECURE_NO_WARNINGS
    CURL_STATICLIB
)
target_include_directories(xiaopengkernel_core PRIVATE ${CMAKE_SOURCE_DIR}/include ${CURL_INCLUDE_DIR})

if(MSVC)
    target_compile_options(xiaopengkernel_core PRIVATE 
        /W4
        /utf-8
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Debug>:/Od>
    )
else()
    target_compile_options(xiaopengkernel_core PRIVATE 
        -Wall
        -Wextra
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g>
    )
endif()

# The main executable
add_executable(${PROJECT_NAME} src/main.cpp $<TARGET_OBJECTS:xiaopengkernel_core>)

if(TARGET quickjs)
    target_include_directories(xiaopengkernel_core PRIVATE "${QUICKJS_ROOT}")
endif()

if(ENABLE_FREETYPE)
    target_include_directories(xiaopengkernel_core PRIVATE 
        "${FREETYPE_ROOT}/include/freetype2"
        "${HARFBUZZ_ROOT}/include/harfbuzz"
    )
endif()

if(EXISTS "${SDL2_ROOT}")
    target_include_directories(xiaopengkernel_core PRIVATE ${SDL2_INCLUDE_DIR})
endif()

if(OPENGL_FOUND)
    target_include_directories(xiaopengkernel_core PRIVATE ${OPENGL_INCLUDE_DIR})
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    _CRT_SECURE_NO_WARNINGS
    CURL_STATICLIB
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /W4
        /utf-8
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Debug>:/Od>
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall
        -Wextra
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g>
    )
endif()

if(MINGW_BUILD)
target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${CURL_ROOT}/lib/libcurl.a
    ${CURL_ROOT}/lib/libnghttp2.a
    ${CURL_ROOT}/lib/libnghttp3.a
    ${CURL_ROOT}/lib/libngtcp2.a
    ${CURL_ROOT}/lib/libngtcp2_crypto_quictls.a
    ${CURL_ROOT}/lib/libssl.a
    ${CURL_ROOT}/lib/libcrypto.a
    ${CURL_ROOT}/lib/libssh2.a
    ${CURL_ROOT}/lib/libz.a
    ${CURL_ROOT}/lib/libzstd.a
    ${CURL_ROOT}/lib/libbrotlidec.a
    ${CURL_ROOT}/lib/libbrotlicommon.a
    ${CURL_ROOT}/lib/libpsl.a
    ws2_32
    crypt32
    wldap32
    secur32
    normaliz
    shlwapi
    bcrypt
)
else()
target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${CURL_LIBRARY}
    curl
    pthread
)
endif()

if(EXISTS "${SDL2_ROOT}" OR SDL2_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        ${XPK_SDL_LINK_LIBRARIES}
    )
endif()

if(TARGET quickjs)
    target_link_libraries(${PROJECT_NAME} PRIVATE quickjs)
endif()

if(ENABLE_FREETYPE)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FREETYPE_LIBRARIES} stdc++ $<$<BOOL:${MINGW_BUILD}>:rpcrt4>)
endif()

if(OPENGL_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${XPK_OPENGL_LIBRARIES})
    # Add OpenGL source files to core
    target_sources(xiaopengkernel_core PRIVATE 
        src/renderer/opengl_canvas.cpp
        src/renderer/opengl_context.cpp
    )
    
    # Add OpenGL test
    add_executable(test_opengl tests/test_opengl.cpp)
    target_include_directories(test_opengl PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(test_opengl PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(test_opengl PRIVATE 
        ${XPK_SDL_LINK_LIBRARIES}
        ${XPK_OPENGL_LIBRARIES}
    )
    set_target_properties(test_opengl PROPERTIES 
        WIN32_EXECUTABLE FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )
    
    # Add simple OpenGL demo
    add_executable(simple_opengl_demo src/renderer/simple_opengl_demo.cpp)
    target_include_directories(simple_opengl_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(simple_opengl_demo PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(simple_opengl_demo PRIVATE 
        ${XPK_SDL_LINK_LIBRARIES}
        ${XPK_OPENGL_LIBRARIES}
    )
    set_target_properties(simple_opengl_demo PROPERTIES 
        WIN32_EXECUTABLE FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )
    
    # Add GPU acceleration test
    add_executable(test_gpu_acceleration tests/test_gpu_acceleration.cpp)
    target_include_directories(test_gpu_acceleration PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(test_gpu_acceleration PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(test_gpu_acceleration PRIVATE 
        ${XPK_SDL_LINK_LIBRARIES}
        ${XPK_OPENGL_LIBRARIES}
    )
    set_target_properties(test_gpu_acceleration PROPERTIES 
        WIN32_EXECUTABLE FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
endif()

enable_testing()

# ═══════════════════════════════════════════════════════════════
#  Unified Test Framework - Helper Functions
# ═══════════════════════════════════════════════════════════════

# Common link libraries for all tests
set(TEST_COMMON_LIBS
    ${CURL_LIBRARY}
)

if(MINGW_BUILD)
    set(TEST_COMMON_LIBS
        ${CURL_ROOT}/lib/libcurl.a
        ${CURL_ROOT}/lib/libnghttp2.a
        ${CURL_ROOT}/lib/libnghttp3.a
        ${CURL_ROOT}/lib/libngtcp2.a
        ${CURL_ROOT}/lib/libngtcp2_crypto_quictls.a
        ${CURL_ROOT}/lib/libssl.a
        ${CURL_ROOT}/lib/libcrypto.a
        ${CURL_ROOT}/lib/libssh2.a
        ${CURL_ROOT}/lib/libz.a
        ${CURL_ROOT}/lib/libzstd.a
        ${CURL_ROOT}/lib/libbrotlidec.a
        ${CURL_ROOT}/lib/libbrotlicommon.a
        ${CURL_ROOT}/lib/libpsl.a
        ws2_32 crypt32 wldap32 secur32 normaliz shlwapi iphlpapi bcrypt
    )
endif()

# Helper function: create a test target with common settings
function(add_xpk_test target_name test_file)
    set(extra_sources ${ARGN})
    add_executable(${target_name} ${test_file} ${extra_sources})
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${target_name} PRIVATE ${TEST_COMMON_LIBS})
    target_compile_definitions(${target_name} PRIVATE CURL_STATICLIB)
    set_target_properties(${target_name} PROPERTIES
        WIN32_EXECUTABLE FALSE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

# Helper: add Freetype to a test target
function(add_xpk_test_freetype target_name)
    if(ENABLE_FREETYPE)
        target_link_libraries(${target_name} PRIVATE ${FREETYPE_LIBRARIES} stdc++ $<$<BOOL:${MINGW_BUILD}>:rpcrt4>)
    endif()
endfunction()

# Helper: add QuickJS to a test target
function(add_xpk_test_quickjs target_name)
    target_include_directories(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/third_party/quickjs")
    target_compile_definitions(${target_name} PRIVATE ENABLE_QUICKJS)
    target_link_libraries(${target_name} PRIVATE xiaopengkernel_core quickjs)
endfunction()

# Helper: add SDL2 to a test target
function(add_xpk_test_sdl target_name)
    if(EXISTS "${SDL2_ROOT}" OR SDL2_FOUND)
        target_link_libraries(${target_name} PRIVATE
            ${XPK_SDL_LINK_LIBRARIES}
        )
        target_compile_definitions(${target_name} PRIVATE SDL_MAIN_HANDLED)
    endif()
    if(OPENGL_FOUND)
        target_link_libraries(${target_name} PRIVATE ${XPK_OPENGL_LIBRARIES})
    endif()
endfunction()

# ═══════════════════════════════════════════════════════════════
#  Test Targets
# ═══════════════════════════════════════════════════════════════

# Layout tests (need font_manager + text_shaper)
add_xpk_test(test_layout tests/test_layout.cpp src/renderer/font_manager.cpp src/renderer/text_shaper.cpp)
add_xpk_test_freetype(test_layout)

# Renderer tests (need bitmap_canvas, painting_algorithm, image, font_manager, text_shaper)
add_xpk_test(test_renderer tests/test_renderer.cpp
    src/renderer/bitmap_canvas.cpp src/renderer/painting_algorithm.cpp
    src/renderer/image.cpp src/renderer/font_manager.cpp src/renderer/text_shaper.cpp)
add_xpk_test_freetype(test_renderer)

# Loader tests (bundle all non-excluded test files)
file(GLOB_RECURSE LOADER_TEST_SOURCES "tests/*.cpp")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_layout\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_renderer\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_parse_thread\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_opengl\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_gpu_acceleration\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_resource_tree\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_flexbox\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_flexbox_advanced\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_positioning\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_dom_binding\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_event\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_incremental\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_grid\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_stacking\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_float\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_event_loop\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_page_lifecycle\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_ifc\\.cpp$")
list(FILTER LOADER_TEST_SOURCES EXCLUDE REGEX "test_phase2_selectors_stacking\\.cpp$")

add_xpk_test(loader_tests ${LOADER_TEST_SOURCES})
add_xpk_test_sdl(loader_tests)

# Individual test targets (each has its own main())
add_xpk_test(test_parse_thread tests/test_parse_thread.cpp)
add_xpk_test(test_resource_tree tests/test_resource_tree.cpp)
add_xpk_test(test_flexbox tests/test_flexbox.cpp)
add_xpk_test(test_flexbox_advanced tests/test_flexbox_advanced.cpp)
add_xpk_test(test_grid tests/test_grid.cpp)
add_xpk_test(test_stacking tests/test_stacking.cpp)
add_xpk_test(test_float tests/test_float.cpp src/renderer/font_manager.cpp src/renderer/text_shaper.cpp)
add_xpk_test_freetype(test_float)
add_xpk_test(test_positioning tests/test_positioning.cpp src/renderer/font_manager.cpp src/renderer/text_shaper.cpp)
add_xpk_test_freetype(test_positioning)
add_xpk_test(test_incremental tests/test_incremental.cpp)
add_xpk_test(test_event tests/test_event.cpp)
add_xpk_test_quickjs(test_event)
add_xpk_test_sdl(test_event)
add_xpk_test_freetype(test_event)
add_xpk_test(test_event_loop tests/test_event_loop.cpp src/engine/event_loop.cpp src/engine/browsing_context.cpp)
add_xpk_test(test_page_lifecycle tests/test_page_lifecycle.cpp
    src/engine/event_loop.cpp src/engine/browsing_context.cpp
    src/engine/page_lifecycle.cpp src/script/promise_binding_simple.cpp)
add_xpk_test(test_ifc tests/test_ifc.cpp src/renderer/font_manager.cpp src/renderer/text_shaper.cpp)
add_xpk_test_freetype(test_ifc)
add_xpk_test(test_dom_binding tests/test_dom_binding.cpp)
add_xpk_test_quickjs(test_dom_binding)
add_xpk_test_sdl(test_dom_binding)
add_xpk_test_freetype(test_dom_binding)
add_xpk_test(test_phase2_selectors_stacking tests/test_phase2_selectors_stacking.cpp)

add_custom_target(run
    COMMAND ${PROJECT_NAME}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

## Minimal Demo: standalone executable for end-to-end demonstration
add_executable(minimal_demo src/demo_minimal_main.cpp $<TARGET_OBJECTS:xiaopengkernel_core>)
target_include_directories(minimal_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MINGW_BUILD)
target_link_libraries(minimal_demo PRIVATE
  ${CURL_ROOT}/lib/libcurl.a
  ${CURL_ROOT}/lib/libnghttp2.a
  ${CURL_ROOT}/lib/libnghttp3.a
  ${CURL_ROOT}/lib/libngtcp2.a
  ${CURL_ROOT}/lib/libngtcp2_crypto_quictls.a
  ${CURL_ROOT}/lib/libssl.a
  ${CURL_ROOT}/lib/libcrypto.a
  ${CURL_ROOT}/lib/libssh2.a
  ${CURL_ROOT}/lib/libz.a
  ${CURL_ROOT}/lib/libzstd.a
  ${CURL_ROOT}/lib/libbrotlidec.a
  ${CURL_ROOT}/lib/libbrotlicommon.a
  ${CURL_ROOT}/lib/libpsl.a
  ws2_32 crypt32 wldap32 secur32 normaliz shlwapi iphlpapi bcrypt
)
else()
target_link_libraries(minimal_demo PRIVATE ${CURL_LIBRARY})
endif()
if(EXISTS "${SDL2_ROOT}" OR SDL2_FOUND)
  target_link_libraries(minimal_demo PRIVATE ${XPK_SDL_LINK_LIBRARIES})
endif()
if(OPENGL_FOUND)
  target_link_libraries(minimal_demo PRIVATE ${XPK_OPENGL_LIBRARIES})
endif()
if(TARGET quickjs)
  target_link_libraries(minimal_demo PRIVATE quickjs)
endif()
if(ENABLE_FREETYPE)
  target_link_libraries(minimal_demo PRIVATE ${FREETYPE_LIBRARIES} stdc++ $<$<BOOL:${MINGW_BUILD}>:rpcrt4>)
endif()
set_target_properties(minimal_demo PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
add_custom_target(run_tests
    COMMAND loader_tests
    DEPENDS loader_tests
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake_clean.cmake
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_SOURCE_DIR}/bin"
)
