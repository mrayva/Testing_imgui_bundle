# Quick Start Guide

## 5-Minute Setup

### 1. Initialize Database

```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"

// Get singleton instance
DatabaseManager& db = DatabaseManager::Get();

// Choose your mode:
db.Initialize(DatabaseConfig::Memory());                    // Fast, no persistence
db.Initialize(DatabaseConfig::NativeFile("./app.db"));      // Disk file
db.Initialize(DatabaseConfig::OPFS("/opfs/app.db"));        // Browser (WASM)
```

### 2. Create Repository

```cpp
FooRepository repo(db);
repo.CreateTable();
```

### 3. Use It!

```cpp
// Insert
repo.Insert(1, "Alice", true);
repo.Insert(2, "Bob", false);

// Query
auto rows = repo.SelectAll();
for (const auto& row : rows) {
    std::cout << row.id << ": " << row.name << std::endl;
}

// Single record
auto alice = repo.SelectById(1);
if (alice) {
    std::cout << alice->name << std::endl;
}

// Update
repo.Update(1, "Alice Smith", true);

// Delete
repo.Delete(2);
```

## Performance Tips

### ✅ Use Transactions for Bulk Operations

```cpp
db.GetConnection()("BEGIN TRANSACTION;");
for (int i = 0; i < 1000; i++) {
    repo.Insert(i, "User " + std::to_string(i), true);
}
db.GetConnection()("COMMIT;");
// 100x faster than individual inserts!
```

### ✅ OPFS Mode Auto-Tunes for Speed

```cpp
// These are applied automatically for OPFS:
// - 16MB cache (minimizes I/O)
// - WAL journal mode
// - NORMAL synchronous
// Result: 10-100x faster than defaults!
```

### ✅ Custom Tuning

```cpp
PerformanceTuning tuning = PerformanceTuning::ForOPFS();
tuning.cache_size_kb = 32768;  // 32MB for large datasets

db.Initialize(DatabaseConfig::OPFS("/opfs/app.db", tuning));
```

## Common Patterns

### GUI Integration (ImGui)

```cpp
static FooRepository* repo = nullptr;
static std::vector<std::string> results;

if (!repo) {
    repo = new FooRepository(DatabaseManager::Get());
    repo->CreateTable();
}

if (ImGui::Button("Add Row")) {
    static int id = 1;
    repo->Insert(id++, "User", true);
}

if (ImGui::Button("Show All")) {
    results = repo->SelectAllAsStrings();
}

for (const auto& row : results) {
    ImGui::Text("%s", row.c_str());
}
```

### Error Handling

```cpp
if (!repo.Insert(1, "Test", true)) {
    std::cerr << "Error: " << repo.GetLastError() << std::endl;
}
```

### Multiple Tables

```cpp
DatabaseManager& db = DatabaseManager::Get();
db.Initialize(DatabaseConfig::Memory());

FooRepository fooRepo(db);      // Your demo table
UserRepository userRepo(db);    // Your user table
ProductRepository prodRepo(db); // Your product table

// All share the same connection
```

## Next Steps

- **Full examples**: See `EXAMPLES.md`
- **OPFS tuning**: Read `PERFORMANCE.md` for 10-100x speedup
- **Architecture**: Check `ARCHITECTURE.md` for design details
- **Migration**: See `MIGRATION.md` if upgrading

## Troubleshooting

**"Database not initialized"**
→ Call `db.Initialize()` before creating repositories

**"OPFS mode only available in WebAssembly"**
→ OPFS is browser-only, use NativeFile for desktop

**Slow inserts in OPFS**
→ Use transactions! See Performance Tips above

**"Table already exists"**
→ Call `CreateTable()` only once, or use `CREATE TABLE IF NOT EXISTS`

## One-Liner Examples

```cpp
// Memory mode, quick test
DatabaseManager::Get().Initialize(DatabaseConfig::Memory());

// Production file mode
DatabaseManager::Get().Initialize(DatabaseConfig::NativeFile("./production.db"));

// Browser persistent mode
DatabaseManager::Get().Initialize(DatabaseConfig::OPFS("/opfs/app.db"));

// Disable auto-tuning
DatabaseManager::Get().Initialize(DatabaseConfig::OPFS("/opfs/app.db", 
                                  PerformanceTuning::Disabled()));
```

## That's It!

You now have a fully functional, mode-aware, performance-tuned database system. 

For more details, see the other documentation files.
