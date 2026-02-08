# C++ Modern EDSL Kitchen Sink

A proof-of-concept C++ project integrating cutting-edge libraries using **C++23**. Combines immediate-mode UI, reactive programming, and type-safe SQL in a single application with native and WebAssembly targets.

## Features

- **Dear ImGui Bundle** -- complete UI framework (HelloImGui, ImPlot, ImNodes)
- **Reaction** -- lightweight, header-only reactive programming for C++20/23
- **sqlpp23** -- type-safe embedded DSL for SQL, pushing C++23 to its limit
- **SQLite3** -- database layer with memory, native-file, and OPFS modes
- **Cross-platform font loading** -- automatic system font detection (Windows, macOS, Linux)
- **WebAssembly** -- full Emscripten build producing `.wasm` + `.js` + `.data`

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| **GCC** | **14** | gcc-14 / g++-14 specifically (see [gcc-15 bug](#gcc-15-deducing-this-bug) below) |
| CMake | 3.22+ | |
| Ninja | any | recommended over Make |
| vcpkg | latest | `VCPKG_ROOT` or `../vcpkg` relative to project |
| emsdk | latest | WASM builds only |

## Native Linux Build

```bash
export CC=gcc-14
export CXX=g++-14

cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

cmake --build build

# Run
./build/KitchenSinkImgui
```

If vcpkg lives at `../vcpkg` the toolchain file is found automatically and can be omitted.

## WASM Build (Emscripten)

```bash
source ~/emsdk/emsdk_env.sh

cmake -B build_wasm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"

cmake --build build_wasm

# Serve (WASM requires an HTTP server)
python3 -m http.server 8000 -d build_wasm
# Open http://localhost:8000/KitchenSinkImgui.html
```

A custom `index.html` in the project root can also be used as the entry point.

## gcc-15 "Deducing This" Bug

gcc-15 miscompiles sqlpp23 code that relies on C++23 explicit object parameters ("deducing this"). The symptom is template deduction failures in `select()`, `insert_into()`, `update()`, and `delete_from()`. **Use gcc-14 for native builds until this is resolved upstream.**

## Known Issues

**Emscripten + sqlpp23 DSL** -- Emscripten's clang does not yet fully support the C++23 "deducing this" feature used by sqlpp23. Type-safe query DSL calls may fail to compile under WASM. Workarounds:

1. Use raw SQL via `connection.execute()` / prepared statements.
2. Gate sqlpp23 DSL calls with `#ifndef __EMSCRIPTEN__` and provide raw-SQL fallbacks.

The DatabaseManager, OPFS mode, and Backup API all work correctly in WASM -- only the sqlpp23 compile-time DSL is affected.

## License

MIT
