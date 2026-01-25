---
description: How to build the application for Web/WASM using Emscripten
---

# Building for Web/WASM

This project supports WebAssembly via Emscripten. Follow these steps to build and run the web version.

## Prerequisites

1.  **Emscripten SDK (emsdk)**: Install and activate the latest version.
    ```cmd
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
    emsdk install latest
    emsdk activate latest
    # On Windows:
    emsdk_env.bat
    ```

2.  **Ninja Build System**: On Windows, it is highly recommended to use [Ninja](https://ninja-build.org/) for WASM builds. It is often included with Visual Studio (under Desktop development with C++).

3.  **vcpkg**: Ensure you have a working `vcpkg` installation.

## Build Steps

1.  **Create a build directory**:
    ```cmd
    mkdir build_wasm
    cd build_wasm
    ```

2.  **Configure with Emscripten**:
    The most reliable way to build with `vcpkg` and `Emscripten` is to use vcpkg's toolchain and have it "chain-load" the Emscripten toolchain. 

    **IMPORTANT**: On Windows, you **must** use `-G Ninja` (preferred) or `-G "MinGW Makefiles"`. The default Visual Studio generator will NOT work.

    ```cmd
    # From inside build_wasm
    cmake .. -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
      -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="C:/path/to/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ^
      -DVCPKG_TARGET_TRIPLET=wasm32-emscripten
    ```
    - Replace paths with your actual `vcpkg` and `emsdk` roots.
    - Use `/` for paths even on Windows to avoid escaping issues in CMake.

3.  **Build**:
    ```cmd
    cmake --build .
    ```

4.  **Run with a local server**:
    HelloImGui generates an `.html` file (e.g., `KitchenSinkImgui.html`).
    ```cmd
    # Use any local server, for example:
    python -m http.server 8080
    ```
    Then open `http://localhost:8080/KitchenSinkImgui.html` in your browser.

## Assets
If you want to include custom fonts for the WASM build, place them in an `assets/fonts` directory in the project root. The `CMakeLists.txt` is configured to preload this directory.
