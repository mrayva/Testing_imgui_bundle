# Known Issues

## Emscripten + sqlpp23 C++23 "Deducing This" Bug

**Status:** Known compiler limitation  
**Affects:** WebAssembly builds only  
**Severity:** Blocks type-safe SQL operations in WASM

### Problem

The Emscripten/clang compiler (as of 2026-02) does not fully support C++23 explicit object parameters ("deducing this" feature) used extensively in sqlpp23. This causes compilation failures for operations like:

- `select()` - SELECT queries
- `update()` - UPDATE statements  
- `delete_from()` - DELETE statements

### Error Example

```
error: no matching function for call to 'single_table(sqlpp::table_t<test_db::Foo_>&)'
note: couldn't deduce template parameter 'Statement'
```

### Workaround Options

#### 1. Use Raw SQL (Recommended for WASM)

```cpp
// Instead of type-safe queries
auto rows = db(select(all_of(test_db::foo)).from(test_db::foo));

// Use raw SQL
auto stmt = db.prepare("SELECT * FROM foo");
for (const auto& row : db(stmt)) {
    // Process row
}
```

#### 2. Conditional Compilation

```cpp
#ifdef __EMSCRIPTEN__
    // Use raw SQL for WASM
    db.GetConnection()("INSERT INTO foo VALUES (?, ?, ?)", id, name, fun);
#else
    // Use sqlpp23 for native
    db.GetConnection()(insert_into(test_db::foo).set(
        test_db::foo.Id = id,
        test_db::foo.Name = name,
        test_db::foo.HasFun = fun
    ));
#endif
```

#### 3. Disable SQL Demo in WASM

```cpp
#ifndef __EMSCRIPTEN__
    if (ImGui::CollapsingHeader("Type-safe SQL (sqlpp23) Example")) {
        // sqlpp23 demo code...
    }
#endif
```

### Status by Platform

| Platform | Compiler | sqlpp23 Support |
|----------|----------|-----------------|
| Native Linux | gcc-14 | ✅ Full support |
| Native Windows | gcc-14/MSVC | ✅ Full support |
| Native macOS | Clang 15+ | ✅ Full support |
| WebAssembly | Emscripten 3.1+ | ❌ Blocked by C++23 bug |

### When Will This Be Fixed?

This requires Emscripten to fully implement C++23 explicit object parameters. Track progress:
- Emscripten Issue: https://github.com/emscripten-core/emscripten/issues
- LLVM C++23 Support: https://clang.llvm.org/cxx_status.html

Expected resolution: Emscripten 3.2+ or LLVM 18+

### DatabaseManager Still Works!

**Good news:** The refactored DatabaseManager itself works fine in WASM:
- ✅ Database initialization
- ✅ Mode switching (Memory/OPFS)
- ✅ Performance tuning
- ✅ Connection management
- ✅ Raw SQL queries

Only the sqlpp23 type-safe query DSL is affected.

### Recommendation

For production WASM apps:
1. Use DatabaseManager for connection/mode management
2. Use raw SQL with prepared statements
3. Add your own type-safety layer if needed
4. Wait for Emscripten C++23 support for sqlpp23 DSL

Native builds work perfectly with full sqlpp23 support.
