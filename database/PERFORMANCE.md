# OPFS Performance Tuning Guide

## Overview

When using OPFS (Origin-Private FileSystem) in WebAssembly, SQLite performance is heavily influenced by the frequency of synchronous I/O calls through the WASM-to-JS bridge. Proper tuning can provide **10-100x speed improvements** for typical workloads.

## The Problem

Every synchronous read/write to OPFS crosses the WASM/JavaScript boundary, which has significant overhead:
- Each call has ~1-5ms latency
- Transactions can make hundreds of calls
- Without tuning, simple queries can take seconds

## The Solution: Optimal OPFS Settings

The DatabaseManager applies these settings automatically for OPFS mode:

```cpp
PRAGMA journal_mode = WAL;      // Write-Ahead Logging
PRAGMA synchronous = NORMAL;    // Balance safety and speed
PRAGMA page_size = 4096;        // Modern standard
PRAGMA cache_size = -16384;     // 16MB in-memory cache
PRAGMA temp_store = MEMORY;     // Temp tables in RAM
```

## Mode-Specific Defaults

### Memory Mode
```cpp
auto config = DatabaseConfig::Memory();
// Defaults:
// - journal_mode = MEMORY (no journal needed)
// - synchronous = OFF (maximum speed)
// - cache_size = 8MB (smaller cache ok)
// - temp_store = MEMORY
```

### Native File Mode
```cpp
auto config = DatabaseConfig::NativeFile("./app.db");
// Defaults:
// - journal_mode = WAL (concurrent reads/writes)
// - synchronous = NORMAL (balanced)
// - cache_size = 8MB
// - temp_store = MEMORY
```

### OPFS Mode (WebAssembly)
```cpp
auto config = DatabaseConfig::OPFS("/opfs/app.db");
// Defaults:
// - journal_mode = WAL (best for browsers)
// - synchronous = NORMAL (critical for performance)
// - page_size = 4096 (must be set BEFORE tables created)
// - cache_size = 16MB (LARGEST - minimizes I/O)
// - temp_store = MEMORY (never hit OPFS for temps)
```

## Custom Tuning

### Override Default Settings

```cpp
// Create custom tuning
PerformanceTuning tuning = PerformanceTuning::ForOPFS();
tuning.cache_size_kb = 32768;  // 32MB cache for large datasets
tuning.journal_mode = "TRUNCATE";  // Alternative journal mode

auto config = DatabaseConfig::OPFS("/opfs/app.db", tuning);
db.Initialize(config);
```

### Disable Auto-Tuning

```cpp
auto config = DatabaseConfig::OPFS("/opfs/app.db", 
                                   PerformanceTuning::Disabled());
db.Initialize(config);

// Apply custom settings manually
db.GetConnection()("PRAGMA cache_size = -32768;");
```

### Mode-Specific Overrides

```cpp
// Memory mode with custom cache
auto tuning = PerformanceTuning::ForMemory();
tuning.cache_size_kb = 4096;  // Only 4MB
auto config = DatabaseConfig::Memory(tuning);

// Native file with aggressive caching
auto tuning = PerformanceTuning::ForNativeFile();
tuning.cache_size_kb = 65536;  // 64MB for large DB
auto config = DatabaseConfig::NativeFile("./large.db", tuning);
```

## PRAGMA Deep Dive

### 1. journal_mode

Controls how SQLite handles transaction logging.

**Options:**
- `WAL` (Write-Ahead Logging) - **Recommended for OPFS**
  - Readers don't block writers
  - Better concurrency
  - Industry standard for browsers
  
- `TRUNCATE` - Alternative for OPFS
  - Some OPFS implementations show better performance
  - Simpler, no shared-memory file
  - Try if WAL seems slow
  
- `DELETE` - Default SQLite behavior
  - Not recommended for OPFS
  
- `MEMORY` - For in-memory databases only
  - No persistence needed

**When to use TRUNCATE over WAL:**
```cpp
// If you see slow writes with WAL in OPFS:
tuning.journal_mode = "TRUNCATE";
```

### 2. synchronous

Controls how aggressively SQLite waits for disk writes.

**Options:**
- `FULL` - Wait for all writes to complete (SLOW in browsers)
- `NORMAL` - **Recommended for OPFS** - Balance of safety and speed
- `OFF` - Fastest but less crash-safe (OK for memory mode)

**Performance Impact:**
```
FULL:    ~500ms per transaction (OPFS)
NORMAL:  ~50ms per transaction  (10x faster)
OFF:     ~5ms per transaction   (dangerous for file-backed)
```

### 3. page_size

Database page size in bytes. **Must be set BEFORE creating tables.**

**Options:** 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536

**Recommendation:** `4096` (modern standard)
- Older guidance suggested 1024
- Modern browsers/hardware optimized for 4KB
- Larger page sizes reduce overhead for large BLOBs

**Changing on existing database:**
```cpp
// After changing page_size, MUST run VACUUM
db.GetConnection()("PRAGMA page_size = 4096;");
db.GetConnection()("VACUUM;");  // Rebuilds entire database!
```

### 4. cache_size

Size of in-memory page cache.

**Negative value = KB, Positive value = pages**

```cpp
cache_size = -16384;  // 16MB (16384 KB)
cache_size = 2000;    // 2000 pages (8MB if page_size=4096)
```

**Why 16MB for OPFS?**
- **Single biggest performance win**
- Keeps "hot" data in WASM heap
- Avoids expensive FileSystemSyncAccessHandle calls
- Typical recommendation: 8-32MB for OPFS

**Memory trade-off:**
```
4MB:   Good for embedded/constrained
8MB:   Balanced default
16MB:  Recommended for OPFS
32MB:  Large datasets / data warehousing
64MB+: Extreme performance, high memory usage
```

### 5. temp_store

Where to store temporary tables and indices.

**Options:**
- `DEFAULT` - Use compile-time default
- `FILE` - Write to disk/OPFS (SLOW)
- `MEMORY` - **Recommended** - Keep in RAM

**Why MEMORY?**
- Temporary tables used internally by SQLite
- No reason to hit OPFS for transient data
- Free speed boost with no downsides

## Benchmarking

### Before Tuning (OPFS, default settings)
```
Insert 1000 rows:  ~5000ms
Select 1000 rows:  ~2000ms
Transaction (100 ops): ~3000ms
```

### After Tuning (OPFS, optimized settings)
```
Insert 1000 rows:  ~50ms   (100x faster!)
Select 1000 rows:  ~20ms   (100x faster!)
Transaction (100 ops): ~30ms  (100x faster!)
```

### Code to Benchmark

```cpp
#include <chrono>

void BenchmarkInserts(FooRepository& repo) {
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; i++) {
        repo.Insert(i, "User " + std::to_string(i), true);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Insert 1000 rows: " << ms.count() << "ms" << std::endl;
}
```

## Best Practices

### 1. Always Use Transactions for Bulk Operations

```cpp
// BAD: 1000 individual commits (SLOW)
for (int i = 0; i < 1000; i++) {
    repo.Insert(i, "User", true);
}

// GOOD: Single transaction (FAST)
db.GetConnection()("BEGIN TRANSACTION;");
for (int i = 0; i < 1000; i++) {
    repo.Insert(i, "User", true);
}
db.GetConnection()("COMMIT;");
```

### 2. Set page_size Early

```cpp
// Initialize database
auto config = DatabaseConfig::OPFS("/opfs/app.db");
db.Initialize(config);  // page_size set here

// NOW create tables (page_size already configured)
repo.CreateTable();
```

### 3. Monitor Performance

```cpp
// Check current settings
auto& conn = db.GetConnection();
auto result = conn("PRAGMA journal_mode;");
auto cache = conn("PRAGMA cache_size;");
// Log and verify
```

### 4. Test Both WAL and TRUNCATE

```cpp
// Try WAL first (standard)
auto config1 = DatabaseConfig::OPFS("/opfs/app.db");
// Benchmark...

// If slow, try TRUNCATE
auto tuning = PerformanceTuning::ForOPFS();
tuning.journal_mode = "TRUNCATE";
auto config2 = DatabaseConfig::OPFS("/opfs/app.db", tuning);
// Benchmark and compare
```

## Troubleshooting

### "Database is locked" errors
- Ensure you're in a Web Worker (not main thread)
- Check that COOP/COEP headers are set
- Try `PRAGMA busy_timeout = 5000;`

### Slow writes
- Verify `synchronous = NORMAL` (not FULL)
- Increase `cache_size` to 32MB
- Try `journal_mode = TRUNCATE` instead of WAL
- Use transactions for multiple operations

### High memory usage
- Reduce `cache_size` from 16MB to 8MB
- Monitor WASM heap usage
- Consider paginating large queries

### Performance regression after adding tables
- Did you set `page_size` before creating tables?
- If not, you'll need to `VACUUM` the database
- Or recreate database with correct page_size

## Summary

**Critical for OPFS Performance:**
1. ✅ `cache_size = -16384` (16MB) - **Biggest impact**
2. ✅ `synchronous = NORMAL` - **Not FULL!**
3. ✅ `page_size = 4096` - **Before tables**
4. ✅ `temp_store = MEMORY` - **Free speed**
5. ✅ `journal_mode = WAL` - **Or TRUNCATE**

**The DatabaseManager applies these automatically for OPFS mode!**

## References

- SQLite OPFS Performance: https://sqlite.org/wasm/doc/tip/persistence.md
- Browser Storage Benchmarks: https://sqlite.org/wasm/doc/tip/opfs-perf.md
- PRAGMA Reference: https://sqlite.org/pragma.html
