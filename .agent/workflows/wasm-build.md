---
description: How to build the application for Web/WASM using Emscripten
---

# Building for Web/WASM

This project supports WebAssembly via Emscripten. Follow these steps to build and run the web version.

## Prerequisites

1.  **Emscripten SDK (emsdk)**: Install and activate the latest version.
    ```bash
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
    ./emsdk install latest
    ./emsdk activate latest
    source ./emsdk_env.sh
    ```

2.  **vcpkg for WASM**: You may need a specific triplet for WASM if you use vcpkg.
    Alternatively, HelloImGui can often fetch its own dependencies when building for WASM.

## Build Steps

1.  **Create a build directory**:
    ```bash
    mkdir build_wasm
    cd build_wasm
    ```

2.  **Configure with Emscripten**:
    The most reliable way to build with `vcpkg` and `Emscripten` is to use vcpkg's toolchain and have it "chain-load" the Emscripten toolchain. 

    **Do NOT** use `emcmake` if you are using the command below, as it may conflict with vcpkg's internal logic.

    ```bash
    cmake .. \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=[path/to/vcpkg]/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=[path/to/emsdk]/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
      -DVCPKG_TARGET_TRIPLET=wasm32-emscripten
    ```
    - Replace `[path/to/vcpkg]` with your vcpkg root.
    - Replace `[path/to/emsdk]` with your Emscripten SDK root.

    *Alternatively, if you are NOT using vcpkg for WASM dependencies:*
    ```bash
    emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
    ```

3.  **Build**:
    ```bash
    cmake --build .
    ```

4.  **Run with a local server**:
    If CMake did not generate an `.html` file, use the manual `index.html` from the project root.
    ```bash
    # From inside build_wasm
    cp ../index.html .
    python3 -m http.server 8080
    ```
    Then open `http://localhost:8080/index.html` in your browser.

## Assets
If you want to include custom fonts for the WASM build, place them in an `assets/fonts` directory in the project root. The `CMakeLists.txt` is configured to preload this directory.
