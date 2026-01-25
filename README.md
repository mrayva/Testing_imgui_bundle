# C++ Modern EDSL Kitchen Sink

A proof-of-concept C++ project integrating cutting-edge libraries using **C++23**. This project demonstrates how to combine immediate-mode UI, reactive programming, and type-safe SQL in a single application.

## 🚀 Features

- **Dear ImGui Bundle**: Complete UI framework including `HelloImGui`, `ImPlot`, and `ImNodes`.
- **Reaction (Reactive Programming)**: A lightweight, header-only reactive framework for C++20/23.
- **sqlpp23 (Type-safe SQL)**: A type-safe embedded domain-specific language for SQL queries, pushing C++23 features to their limit.
- **SQLite3 Integration**: In-memory database support using `sqlpp23`.
- **Cross-Platform Font Loading**: Automatic detection of system fonts on Windows, macOS, and Linux.
- **WebAssembly Support**: Full support for building as a web application via Emscripten.

## 🛠 Prerequisites

- **Compiler**: Clang 20+ or GCC 14+ (required for full C++23 support). 
  - *Note: GCC 14 is recommended for native builds to avoid current GCC 15 bugs.*
- **Package Manager**: [vcpkg](https://github.com/microsoft/vcpkg)
- **Build System**: CMake 3.28+ and Ninja.
- **WASM Build**: [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)

## 🏗 Build Instructions

### Native (Linux/Windows/macOS)

1. **Configure and Build**:
   Ensure `vcpkg` is installed and the toolchain path is correctly specified.

   ```bash
   cmake -B build -S . \
     -DCMAKE_TOOLCHAIN_FILE=[path/to/vcpkg]/scripts/buildsystems/vcpkg.cmake \
     -DCMAKE_CXX_COMPILER=g++-14 \
     -DCMAKE_C_COMPILER=gcc-14 \
     -GNinja
   cmake --build build
   ```

2. **Run**:
   ```bash
   ./build/KitchenSinkImgui
   ```

### Web/WASM (Emscripten)

See the detailed [WASM Build Guide](.agent/workflows/wasm-build.md) for instructions on building for the web.

## 📜 License

This project is licensed under the MIT License.
