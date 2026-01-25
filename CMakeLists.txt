cmake_minimum_required(VERSION 3.22)
project(KitchenSinkImgui VERSION 0.1.0 LANGUAGES CXX)

# Workaround for WASM: ensure CMake recognizes the 32-bit architecture to prevent find_package rejections
if (EMSCRIPTEN)
    set(CMAKE_SIZEOF_VOID_P 4 CACHE INTERNAL "Check size of void*" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui Bundle requires some specific setup
include(FetchContent)
FetchContent_Declare(
    imgui_bundle
    GIT_REPOSITORY https://github.com/pthom/imgui_bundle.git
    GIT_TAG        main # Or a specific version/commit
)

# Enable FreeType for high-quality font rendering
set(HELLOIMGUI_USE_FREETYPE ON CACHE BOOL "" FORCE)
set(HELLOIMGUI_WITH_TEST_ENGINE OFF CACHE BOOL "" FORCE)

# Find plutosvg and its dependencies via vcpkg
find_package(plutovg CONFIG REQUIRED)
find_package(plutosvg CONFIG REQUIRED)

# Create aliases so that imgui_bundle can find them by their short names
if (NOT TARGET plutosvg)
    add_library(plutosvg ALIAS plutosvg::plutosvg)
endif()
if (NOT TARGET plutovg)
    add_library(plutovg ALIAS plutovg::plutovg)
endif()

# Handle plutosvg and plutovg includes (prefer targets, but add paths if needed for older FetchContent patterns)
# Note: For WASM, these should be provided by the toolchain or fetched by FetchContent
if (NOT EMSCRIPTEN)
    # Only use these if not on Emscripten, and ideally these should be dynamic
    # find_package should handle this, but if vcpkg is used, it sets the include dirs
endif()

# Enable Backends
set(HELLOIMGUI_USE_GLFW3 ON CACHE BOOL "" FORCE)
set(HELLOIMGUI_HAS_OPENGL3 ON CACHE BOOL "" FORCE)


# sqlpp23 requires a recent compiler (C++23)
set(BUILD_SQLITE3_CONNECTOR ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    sqlpp23
    GIT_REPOSITORY https://github.com/rbock/sqlpp23.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(imgui_bundle sqlpp23)

add_executable(${PROJECT_NAME} main.cpp)

# Link against imgui_bundle.
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_bundle)

# Assets handling for HelloImGui
# This will copy assets to the build directory and handle them for WASM
if (EMSCRIPTEN)
    # The standard way to get .html output in CMake + Emscripten
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".html")

    # Add essential Emscripten link options
    target_link_options(${PROJECT_NAME} PRIVATE
        "-sUSE_GLFW=3"
        "-sASSERTIONS=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets@assets"
    )
endif()

# If you have an assets folder, HelloImGui can use it automatically
# if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
#     hello_imgui_add_assets(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/assets")
# endif()

# Link plutosvg and plutovg via imported targets
target_link_libraries(${PROJECT_NAME} PRIVATE plutosvg::plutosvg plutovg::plutovg freetype)



# Link against reaction
find_package(reaction CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE reaction::reaction)

# Link against sqlpp23 and its sqlite3 connector
# sqlpp23::sqlite3 requires sqlite3 to be found
find_package(SQLite3 REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE sqlpp23 sqlpp23_sqlite3)



# The bundle already includes glfw and glad, but if you need them explicitly:
# find_package(glfw3 CONFIG REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

