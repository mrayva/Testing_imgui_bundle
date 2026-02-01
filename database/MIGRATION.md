# DatabaseManager Refactoring - Migration Guide

## Overview

The DatabaseManager has been refactored to separate concerns and support multiple database modes (Memory, NativeFile, OPFS).

## What Changed

### Before (Old API)
```cpp
#include "database_manager.h"

// Initialization (always memory mode)
g_dbManager.Initialize();

// Table operations directly on manager
g_dbManager.InsertRow(1, "User", true);
auto results = g_dbManager.SelectAllRows();
```

### After (New API)
```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"

// Initialization with explicit mode
g_dbManager.Initialize(DatabaseConfig::Memory());
// or
g_dbManager.Initialize(DatabaseConfig::NativeFile("/path/to/db.db"));

// Table operations through repository
FooRepository fooRepo(g_dbManager);
fooRepo.CreateTable();
fooRepo.Insert(1, "User", true);
auto results = fooRepo.SelectAllAsStrings();
```

## Migration Steps

### 1. Update Includes

**Old:**
```cpp
#include "database_manager.h"
```

**New:**
```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"
```

### 2. Update Initialization

**Old:**
```cpp
DatabaseManager::Get().Initialize();
// or
g_dbManager.Initialize(":memory:");
```

**New:**
```cpp
DatabaseManager::Get().Initialize(DatabaseConfig::Memory());
// or choose a different mode:
g_dbManager.Initialize(DatabaseConfig::NativeFile("./app.db"));
g_dbManager.Initialize(DatabaseConfig::OPFS("/opfs/app.db"));  // WASM only
```

### 3. Create Repository Instance

**New (required):**
```cpp
FooRepository fooRepo(g_dbManager);
fooRepo.CreateTable();
```

### 4. Update Method Calls

| Old Method | New Method |
|------------|------------|
| `g_dbManager.InsertRow(id, name, fun)` | `fooRepo.Insert(id, name, fun)` |
| `g_dbManager.SelectAllRows()` | `fooRepo.SelectAllAsStrings()` |
| N/A | `fooRepo.SelectAll()` (returns structured data) |
| N/A | `fooRepo.SelectById(id)` |
| N/A | `fooRepo.Update(id, name, fun)` |
| N/A | `fooRepo.Delete(id)` |

### 5. Error Handling

**Old:**
```cpp
if (g_dbManager.HasError()) {
    std::cout << g_dbManager.GetLastError();
}
```

**New (check repository errors):**
```cpp
if (fooRepo.HasError()) {
    std::cout << fooRepo.GetLastError();
}
// Database manager also has its own errors
if (g_dbManager.HasError()) {
    std::cout << g_dbManager.GetLastError();
}
```

## Complete Example

### Old Code
```cpp
#include "database_manager.h"

int main() {
    DatabaseManager& db = DatabaseManager::Get();
    
    if (!db.Initialize()) {
        return 1;
    }
    
    db.InsertRow(1, "Alice", true);
    db.InsertRow(2, "Bob", false);
    
    auto rows = db.SelectAllRows();
    for (const auto& row : rows) {
        std::cout << row << std::endl;
    }
    
    return 0;
}
```

### New Code
```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"

int main() {
    DatabaseManager& db = DatabaseManager::Get();
    
    if (!db.Initialize(DatabaseConfig::Memory())) {
        return 1;
    }
    
    FooRepository fooRepo(db);
    if (!fooRepo.CreateTable()) {
        return 2;
    }
    
    fooRepo.Insert(1, "Alice", true);
    fooRepo.Insert(2, "Bob", false);
    
    auto rows = fooRepo.SelectAllAsStrings();
    for (const auto& row : rows) {
        std::cout << row << std::endl;
    }
    
    return 0;
}
```

## Benefits of New Design

1. **Mode Support**: Easy to switch between memory, file, and OPFS modes
2. **Separation of Concerns**: Connection management separate from table operations
3. **Extensibility**: Add new tables without modifying DatabaseManager
4. **Type Safety**: Repository pattern provides better API for each table
5. **Testability**: Can mock repositories independently

## Creating Custom Repositories

```cpp
#include "database/database_manager.h"
#include "database/schemas/my_table.h"

class MyTableRepository {
public:
    explicit MyTableRepository(DatabaseManager& db) : m_db(db) {}
    
    bool CreateTable() {
        try {
            m_db.GetConnection()("CREATE TABLE my_table (...)");
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }
    
    // Add your methods...
    
private:
    DatabaseManager& m_db;
    std::string m_lastError;
};
```

## Notes

- The old `database_manager.h` file has been removed from the root directory
- `table_foo.h` moved from root to `database/schemas/`
- FooRepository is just a demo - create your own repositories as needed
- All changes maintain backward compatibility in functionality (memory mode default)
