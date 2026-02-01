# C++ Modern EDSL Kitchen Sink

A proof-of-concept C++ project integrating cutting-edge libraries using **C++23**. This project demonstrates how to combine immediate-mode UI, reactive programming, and type-safe SQL in a single application.

## 🚀 Features

- **Dear ImGui Bundle**: Complete UI framework including `HelloImGui`, `ImPlot`, and `ImNodes`.
- **Reaction (Reactive Programming)**: A lightweight, header-only reactive framework for C++20/23.
- **sqlpp23 (Type-safe SQL)**: A type-safe embedded domain-specific language for SQL queries, pushing C++23 features to their limit.
- **SQLite3 Integration**: Refactored database layer with repository pattern supporting:
  - Memory-only databases
  - Native file-based databases
  - OPFS (Origin Private File System) for WebAssembly with automatic performance tuning
  - See [`database/`](database/) for comprehensive documentation
- **Cross-Platform Font Loading**: Automatic detection of system fonts on Windows, macOS, and Linux.
- **WebAssembly Support**: Full support for building as a web application via Emscripten.

## 🛠 Prerequisites

- **Compiler**: Clang 20+ or GCC 14+ (required for full C++23 support). 
  - *Note: GCC 14 is recommended for native builds to avoid current GCC 15 bugs.*
- **Package Manager**: [vcpkg](https://github.com/microsoft/vcpkg)
- **Build System**: CMake 3.28+ and **Ninja** (highly recommended for WASM).
- **WASM Build**: [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)

## 🏗 Build Instructions

### Native (Linux/Windows/macOS)

**⚠️ Important**: Use **GCC 14** to avoid C++23 "deducing this" bugs in GCC 15.

```bash
# Set compiler to gcc-14
export CC=gcc-14
export CXX=g++-14

# Configure and build
rm -rf build && mkdir build && cd build
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=[path/to/vcpkg]/scripts/buildsystems/vcpkg.cmake \
  ..
ninja

# Run
./KitchenSinkImgui
```

**Alternative** (if vcpkg is in parent directory):
```bash
export CC=gcc-14 CXX=g++-14
cmake -G Ninja .. && ninja
```

### Web/WASM (Emscripten)

**⚠️ Key Discovery**: Use **gcc-14** with emcmake to get full sqlpp23 support!

```bash
# Activate Emscripten
source ~/emsdk/emsdk_env.sh

# Configure with gcc-14 (NOT emcc!)
rm -rf build_wasm && mkdir build_wasm && cd build_wasm
emcmake cmake -G Ninja \
  -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_C_COMPILER=gcc-14 \
  ..
ninja
```

This produces a native x86-64 binary that works with the WASM build system while avoiding Emscripten's C++23 limitations.

**Result**: Full sqlpp23 type-safe SQL works in both native and WASM builds!

See [`BUILD_STATUS.md`](BUILD_STATUS.md) for detailed build results and [`database/`](database/) for the new database architecture.

## 📜 License

This project is licensed under the MIT License.
