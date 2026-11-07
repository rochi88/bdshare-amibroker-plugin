cmake_minimum_required(VERSION 3.16)
project(BDShare_DSE VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# ── Unit tests ───────────────────────────────────────────────────────────────
# The parsers and bar logic are platform-neutral, so these run on any OS.
add_executable(bdshare_tests
    tests/test_main.cpp
    src/DSEParsers.cpp
)
target_include_directories(bdshare_tests PRIVATE include)
target_compile_definitions(bdshare_tests PRIVATE
    FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/"
)
if(NOT WIN32)
    # Lets include/Plugin.h compile without the Windows SDK.
    target_include_directories(bdshare_tests PRIVATE tests/stub)
endif()
add_test(NAME bdshare_tests COMMAND bdshare_tests)

# ── The plugin DLL is Windows-only ───────────────────────────────────────────
if(NOT WIN32)
    return()
endif()

# AmiBroker loads plugins from its Plugins\ folder.
# The DLL must be named *.DLL (any name) but we choose a descriptive one.
set(TARGET_NAME BDShare_DSE)

add_library(${TARGET_NAME} SHARED
    src/Plugin.cpp
    src/DSEFetcher.cpp
    src/DSEParsers.cpp
    src/Plugin.def
)

target_include_directories(${TARGET_NAME} PRIVATE include)

# ── Windows libraries ────────────────────────────────────────────────────────
target_link_libraries(${TARGET_NAME} PRIVATE
    winhttp     # WinHTTP HTTP client
    user32      # MessageBox, PostMessage, FindWindow
)

# ── Version, in one place. ADK format: MAJOR*10000 + MINOR*100 + PATCH ───────
math(EXPR BDSHARE_VERSION_NUM
    "${PROJECT_VERSION_MAJOR}*10000 + ${PROJECT_VERSION_MINOR}*100 + ${PROJECT_VERSION_PATCH}")
target_compile_definitions(${TARGET_NAME} PRIVATE
    BDSHARE_VERSION_NUM=${BDSHARE_VERSION_NUM}
    BDSHARE_VERSION_STRING="${PROJECT_VERSION}"
    BDSHARE_VERSION_WSTRING=L"${PROJECT_VERSION}"
)

# ── Compiler options ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /W4         # high warning level
        /WX-        # don't treat warnings as errors (ADK headers have some)
        /MP         # multi-processor compilation
        /EHsc       # standard C++ exception handling
    )

    # Produce both 32-bit (x86) and 64-bit builds depending on the selected
    # generator platform.  AmiBroker 6.x ships as 32-bit; 64-bit version also
    # available.  Match the plugin architecture to the installed AmiBroker.
    target_compile_definitions(${TARGET_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# ── Post-build: copy to AmiBroker Plugins folder automatically ───────────────
# Set AMIBROKER_PATH in CMake cache or environment to activate auto-deploy.
if(DEFINED ENV{AMIBROKER_PATH})
    set(AB_PLUGINS_DIR "$ENV{AMIBROKER_PATH}/Plugins")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AB_PLUGINS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE:${TARGET_NAME}>
                "${AB_PLUGINS_DIR}/$<TARGET_FILE_NAME:${TARGET_NAME}>"
        COMMENT "Deploying ${TARGET_NAME}.dll → ${AB_PLUGINS_DIR}"
    )
endif()
