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
- **Build System**: CMake 3.28+ and **Ninja** (highly recommended for WASM).
- **WASM Build**: [Emscripten SDK (emsdk)](https://emscripten.org/docs/getting_started/downloads.html)

## 🏗 Build Instructions

### Native (Linux/Windows/macOS)

1. **Configure and Build**:
   Ensure `vcpkg` is installed and the toolchain path is correctly specified.

   ```bash
   cmake -B build -S . \
     -DCMAKE_TOOLCHAIN_FILE=[path/to/vcpkg]/scripts/buildsystems/vcpkg.cmake \
     -GNinja
   cmake --build build
   ```

### Web/WASM (Emscripten)

Modern WASM builds using `vcpkg` and `imgui_bundle` on Windows require specific steps to handle dependencies and generators correctly.

**Brief Steps for Windows**:
1. Run `emsdk_env.bat` to activate Emscripten.
2. Use `-G Ninja` in your CMake command.
3. Use the `wasm32-emscripten` vcpkg triplet.

See the [Detailed WASM Build Guide](.agent/workflows/wasm-build.md) for the exact commands.

## 📜 License

This project is licensed under the MIT License.
