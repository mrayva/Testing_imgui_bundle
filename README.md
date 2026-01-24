# C++ Modern EDSL Kitchen Sink

A proof-of-concept C++ project integrating cutting-edge libraries using **C++23**. This project demonstrates how to combine immediate-mode UI, reactive programming, and type-safe SQL in a single application.

## 🚀 Features

- **Dear ImGui Bundle**: Complete UI framework including `HelloImGui`, `ImPlot`, and `ImNodes`.
- **Reaction (Reactive Programming)**: A lightweight, header-only reactive framework for C++20/23.
- **sqlpp23 (Type-safe SQL)**: A type-safe embedded domain-specific language for SQL queries, pushing C++23 features to their limit.
- **SQLite3 Integration**: In-memory database support using `sqlpp23`.

## 🛠 Prerequisites

- **Compiler**: Clang 20 or later (required for full C++23 "Deducing This" support). 
  - *Note: GCC 15 currently has lookup bugs with `sqlpp23`'s implementation.*
- **Package Manager**: [vcpkg](https://github.com/microsoft/vcpkg)
- **Build System**: CMake 3.28+ and Ninja.

## 🏗 Build Instructions

1. **Clone the repository**:
   ```bash
   git clone <repository-url>
   cd testimguibundle
   ```

2. **Configure and Build**:
   I've configured the project to use your local vcpkg. Ensure the toolchain path is correct for your environment.

   ```bash
   cmake -B build -S . \
     -DCMAKE_TOOLCHAIN_FILE=/home/mrayva/vcpkg/scripts/buildsystems/vcpkg.cmake \
     -DCMAKE_CXX_COMPILER=clang++ \
     -GNinja
   cmake --build build
   ```

3. **Run**:
   ```bash
   ./build/KitchenSinkImgui
   ```

## 📜 License

This project is licensed under the MIT License.
