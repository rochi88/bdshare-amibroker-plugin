cmake_minimum_required(VERSION 3.16)
project(BDShare_DSE VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Output DLL name ──────────────────────────────────────────────────────────
# AmiBroker loads plugins from its Plugins\ folder.
# The DLL must be named *.DLL (any name) but we choose a descriptive one.
set(TARGET_NAME BDShare_DSE)

# ── Sources ──────────────────────────────────────────────────────────────────
add_library(${TARGET_NAME} SHARED
    src/Plugin.cpp
    src/DSEFetcher.cpp
    src/Plugin.def
)

# ── Include dirs ─────────────────────────────────────────────────────────────
target_include_directories(${TARGET_NAME} PRIVATE include)

# ── Windows libraries ────────────────────────────────────────────────────────
target_link_libraries(${TARGET_NAME} PRIVATE
    winhttp     # WinHTTP HTTP client
    user32      # MessageBox, PostMessage, FindWindow
)

# ── Compiler options ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE
        /W4         # high warning level
        /WX-        # don't treat warnings as errors (ADK headers have some)
        /MP         # multi-processor compilation
        /EHsc       # standard C++ exception handling
        /DUNICODE /D_UNICODE
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
