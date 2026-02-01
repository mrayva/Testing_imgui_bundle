# DatabaseManager Examples

## Quick Start

### Memory Database (Default)
```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"

DatabaseManager& db = DatabaseManager::Get();
db.Initialize(DatabaseConfig::Memory());

FooRepository repo(db);
repo.CreateTable();
repo.SeedDemoData();
```

### Native File Database
```cpp
// Persistent SQLite file on disk
db.Initialize(DatabaseConfig::NativeFile("./myapp.db"));
FooRepository repo(db);
repo.CreateTable();
```

### OPFS Database (WebAssembly)
```cpp
#ifdef __EMSCRIPTEN__
// Browser-side persistent storage with optimal performance tuning
db.Initialize(DatabaseConfig::OPFS("/opfs/myapp.db"));
// Automatically applies:
// - journal_mode = WAL
// - synchronous = NORMAL  
// - cache_size = 16MB (minimizes I/O)
// - page_size = 4096
// - temp_store = MEMORY
FooRepository repo(db);
repo.CreateTable();
#endif
```

### Custom Performance Tuning
```cpp
// Override default OPFS settings for extreme performance
PerformanceTuning tuning = PerformanceTuning::ForOPFS();
tuning.cache_size_kb = 32768;  // 32MB cache for large datasets
tuning.journal_mode = "TRUNCATE";  // Try if WAL is slow

db.Initialize(DatabaseConfig::OPFS("/opfs/myapp.db", tuning));
```

## CRUD Operations

### Insert
```cpp
FooRepository repo(db);
repo.Insert(1, "Alice", true);
repo.Insert(2, "Bob", false);
```

### Select All
```cpp
// As formatted strings (for GUI display)
auto strings = repo.SelectAllAsStrings();
for (const auto& str : strings) {
    ImGui::Text("%s", str.c_str());
}

// As structured data
auto rows = repo.SelectAll();
for (const auto& row : rows) {
    std::cout << row.id << ": " << row.name 
              << " (fun=" << row.has_fun << ")" << std::endl;
}
```

### Select by ID
```cpp
auto row = repo.SelectById(1);
if (row) {
    std::cout << "Found: " << row->name << std::endl;
} else {
    std::cout << "Not found" << std::endl;
}
```

### Update
```cpp
repo.Update(1, "Alice Smith", true);
```

### Delete
```cpp
repo.Delete(1);
```

## Error Handling

```cpp
if (!repo.Insert(1, "Test", true)) {
    if (repo.HasError()) {
        std::cerr << "Error: " << repo.GetLastError() << std::endl;
        repo.ClearError();
    }
}
```

## Multiple Tables

```cpp
// Create multiple repositories for different tables
DatabaseManager& db = DatabaseManager::Get();
db.Initialize(DatabaseConfig::Memory());

FooRepository fooRepo(db);
UserRepository userRepo(db);
ProductRepository productRepo(db);

// Each repository manages its own table
fooRepo.CreateTable();
userRepo.CreateTable();
productRepo.CreateTable();

// All share the same database connection
fooRepo.Insert(1, "Demo", true);
userRepo.Insert(1, "Alice", "alice@example.com");
productRepo.Insert(1, "Widget", 9.99);
```

## Mode Detection

```cpp
DatabaseManager& db = DatabaseManager::Get();

switch (db.GetMode()) {
    case DatabaseMode::Memory:
        std::cout << "Using in-memory database" << std::endl;
        break;
    case DatabaseMode::NativeFile:
        std::cout << "Using disk file" << std::endl;
        break;
    case DatabaseMode::OPFS:
        std::cout << "Using browser OPFS" << std::endl;
        break;
}
```

## Direct SQL Access

```cpp
// For custom queries not covered by repositories
auto& conn = db.GetConnection();
conn("CREATE INDEX idx_name ON foo(name)");
conn("PRAGMA cache_size = -8192");  // 8MB cache
```

## GUI Integration (ImGui)

```cpp
void RenderDatabaseUI() {
    static FooRepository* repo = nullptr;
    static std::vector<std::string> results;
    
    if (!repo) {
        DatabaseManager& db = DatabaseManager::Get();
        if (db.IsInitialized()) {
            repo = new FooRepository(db);
            repo->CreateTable();
        }
    }
    
    if (repo && ImGui::Begin("Database Demo")) {
        if (ImGui::Button("Insert Random Row")) {
            static int id = 1;
            repo->Insert(id++, "User " + std::to_string(id), true);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            results = repo->SelectAllAsStrings();
        }
        
        ImGui::Separator();
        for (const auto& row : results) {
            ImGui::Text("- %s", row.c_str());
        }
        
        ImGui::End();
    }
}
```

## Performance Tuning

### Automatic (Mode-Specific)
```cpp
// Each mode gets optimal defaults automatically
DatabaseManager& db = DatabaseManager::Get();

// OPFS: 16MB cache, WAL journal, NORMAL sync
db.Initialize(DatabaseConfig::OPFS("/opfs/app.db"));

// Native: 8MB cache, WAL journal, NORMAL sync  
db.Initialize(DatabaseConfig::NativeFile("./app.db"));

// Memory: 8MB cache, MEMORY journal, OFF sync
db.Initialize(DatabaseConfig::Memory());
```

### Manual Tuning
```cpp
// For heavy workloads, tune cache settings
db.GetConnection()("PRAGMA cache_size = -8192");  // 8MB
db.GetConnection()("PRAGMA temp_store = MEMORY");
db.GetConnection()("PRAGMA journal_mode = WAL");   // Write-ahead log
db.GetConnection()("PRAGMA synchronous = NORMAL");
```

### Custom Performance Profile
```cpp
// Create custom tuning
PerformanceTuning tuning;
tuning.cache_size_kb = 32768;     // 32MB
tuning.journal_mode = "TRUNCATE"; // Alternative to WAL
tuning.synchronous = "NORMAL";
tuning.page_size = 4096;

auto config = DatabaseConfig::OPFS("/opfs/app.db", tuning);
db.Initialize(config);
```

### Disable Auto-Tuning
```cpp
// Use SQLite defaults (not recommended for OPFS)
auto config = DatabaseConfig::OPFS("/opfs/app.db", 
                                   PerformanceTuning::Disabled());
db.Initialize(config);
```

### Benchmarking
```cpp
#include <chrono>

void BenchmarkInserts(FooRepository& repo) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Use transaction for bulk operations
    db.GetConnection()("BEGIN TRANSACTION;");
    for (int i = 0; i < 1000; i++) {
        repo.Insert(i, "User " + std::to_string(i), true);
    }
    db.GetConnection()("COMMIT;");
    
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Inserted 1000 rows in " << ms.count() << "ms" << std::endl;
}
```

## Testing Different Modes

```cpp
void TestAllModes() {
    // Test 1: Memory
    {
        DatabaseManager db1;
        db1.Initialize(DatabaseConfig::Memory());
        FooRepository repo(db1);
        repo.CreateTable();
        repo.Insert(1, "Test", true);
        assert(repo.SelectAll().size() == 1);
    }
    
    // Test 2: File
    {
        DatabaseManager db2;
        db2.Initialize(DatabaseConfig::NativeFile("/tmp/test.db"));
        FooRepository repo(db2);
        repo.CreateTable();
        repo.Insert(1, "Test", true);
        assert(repo.SelectAll().size() == 1);
    }
    
    // Data persists across connections
    {
        DatabaseManager db3;
        db3.Initialize(DatabaseConfig::NativeFile("/tmp/test.db"));
        FooRepository repo(db3);
        assert(repo.SelectAll().size() == 1);  // Still there!
    }
}
```

---

## SQLite Online Backup API Examples

The fastest way to load databases in WASM (10-100x faster than INSERT INTO ... SELECT).

### Load OPFS Database into Memory (Blocking)

```cpp
#include "database/database_manager.h"
#include "database/repositories/user_repository.h"

int main() {
    // 1. Create fast in-memory database
    DatabaseManager& db = DatabaseManager::Get();
    db.Initialize(DatabaseConfig::Memory());
    
    // 2. Load entire OPFS database into memory
    //    This is 10-100x faster than INSERT INTO ... SELECT
    if (!db.BackupFromFile("/opfs/userdata.db")) {
        std::cerr << "No saved data, starting fresh" << std::endl;
    }
    
    // 3. Database is now in memory - blazing fast!
    auto userRepo = new UserRepository(db);
    userRepo->CreateTable();  // Safe even if already loaded
    
    // Work with data at RAM speed...
    auto users = userRepo->SelectAll();
    
    return 0;
}
```

**Why?** OPFS has synchronous I/O overhead. Loading into memory gives you OPFS persistence with RAM-speed queries!

### Load Large Database with Progress (Non-Blocking)

```cpp
#include "database/database_manager.h"
#include <iostream>

void LoadDatabaseWithProgress() {
    DatabaseManager& db = DatabaseManager::Get();
    
    // Memory database with OPFS tuning
    auto config = DatabaseConfig::Memory();
    config.tuning = PerformanceTuning::ForOPFS();
    db.Initialize(config);
    
    std::cout << "Loading database..." << std::endl;
    
    // Load 100 pages at a time (non-blocking)
    bool success = db.BackupFromFileIncremental(
        "/opfs/large.db",
        100,  // pages per step
        [](int remaining, int total) {
            int percent = 100 * (total - remaining) / total;
            std::cout << "\rProgress: " << percent << "%     " << std::flush;
        }
    );
    
    if (success) {
        std::cout << "\nDatabase loaded successfully!" << std::endl;
    } else {
        std::cerr << "\nLoad failed: " << db.GetLastError() << std::endl;
    }
}
```

**Use when:** Database is >10MB and UI responsiveness matters.

### WASM: Load on Startup, Save on Shutdown

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void InitializeApp() {
    DatabaseManager& db = DatabaseManager::Get();
    
    // 1. Create memory database
    db.Initialize(DatabaseConfig::Memory());
    
    // 2. Try to load persisted data
    if (db.BackupFromFile("/opfs/app.db")) {
        std::cout << "Restored previous session" << std::endl;
    } else {
        std::cout << "First run, creating default data" << std::endl;
        // Seed default data...
    }
}

void ShutdownApp() {
    // Save all changes to OPFS
    if (!DatabaseManager::Get().BackupToFile("/opfs/app.db")) {
        std::cerr << "Failed to save!" << std::endl;
    }
}

#ifdef __EMSCRIPTEN__
// Register shutdown handler
EM_JS(void, RegisterShutdownHandler, (), {
    window.addEventListener('beforeunload', function() {
        Module._ShutdownApp();  // Calls C++ function
    });
});
#endif

int main() {
    InitializeApp();
    
    #ifdef __EMSCRIPTEN__
    RegisterShutdownHandler();
    #endif
    
    // Run app...
    
    #ifndef __EMSCRIPTEN__
    ShutdownApp();  // Desktop: explicit shutdown
    #endif
}
```

**Best pattern for WASM**: Memory speed + OPFS persistence!

### Periodic Auto-Save

```cpp
#include <chrono>

class AutoSaveManager {
    std::chrono::steady_clock::time_point m_lastSave;
    bool m_dirty = false;
    
public:
    AutoSaveManager() : m_lastSave(std::chrono::steady_clock::now()) {}
    
    void MarkDirty() {
        m_dirty = true;
    }
    
    void Update() {
        if (!m_dirty) return;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastSave);
        
        // Auto-save every 5 minutes
        if (elapsed.count() >= 300) {
            if (DatabaseManager::Get().BackupToFile("/opfs/app.db")) {
                std::cout << "Auto-saved" << std::endl;
                m_dirty = false;
                m_lastSave = now;
            }
        }
    }
};

// Global instance
AutoSaveManager g_autoSave;

// In repositories, mark dirty on changes
void UserRepository::Insert(int id, const std::string& name) {
    // ... normal insert ...
    g_autoSave.MarkDirty();
}

// In main loop
void Gui() {
    // ... UI code ...
    
    // Check if auto-save needed
    g_autoSave.Update();
}
```

### Performance Comparison Example

```cpp
#include <chrono>
#include <iostream>

void ComparePerformance() {
    using namespace std::chrono;
    
    // Method 1: INSERT INTO ... SELECT (slow)
    {
        DatabaseManager db1, db2;
        db1.Initialize(DatabaseConfig::NativeFile("source.db"));
        db2.Initialize(DatabaseConfig::Memory());
        
        auto start = steady_clock::now();
        
        // Slow way
        db2.GetConnection()("ATTACH DATABASE 'source.db' AS src");
        db2.GetConnection()("INSERT INTO main.users SELECT * FROM src.users");
        db2.GetConnection()("DETACH DATABASE src");
        
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start);
        std::cout << "INSERT INTO ... SELECT: " << elapsed.count() << "ms" << std::endl;
    }
    
    // Method 2: Backup API (fast)
    {
        DatabaseManager& db = DatabaseManager::Get();
        db.Initialize(DatabaseConfig::Memory());
        
        auto start = steady_clock::now();
        
        // Fast way
        db.BackupFromFile("source.db");
        
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start);
        std::cout << "Backup API: " << elapsed.count() << "ms" << std::endl;
    }
    
    // Typical result: Backup API is 10-100x faster!
}
```

### Error Handling with Backup

```cpp
bool InitializeDatabaseSafely() {
    DatabaseManager& db = DatabaseManager::Get();
    
    // 1. Create memory database
    if (!db.Initialize(DatabaseConfig::Memory())) {
        std::cerr << "Failed to initialize database" << std::endl;
        return false;
    }
    
    // 2. Try to load from OPFS
    if (!db.BackupFromFile("/opfs/app.db")) {
        // Not an error - might be first run
        std::cout << "Starting with empty database: " << db.GetLastError() << std::endl;
        
        // Create schema
        auto repo = new UserRepository(db);
        repo->CreateTable();
        repo->SeedDefaultData();
        
        // Save initial state
        if (!db.BackupToFile("/opfs/app.db")) {
            std::cerr << "Warning: Could not save initial database" << std::endl;
        }
    }
    
    return true;
}
```

---

## See Also

- **[BACKUP_API.md](BACKUP_API.md)** - Complete backup API guide (550+ lines)
- **[PERFORMANCE.md](PERFORMANCE.md)** - OPFS optimization guide
- **[QUICK_START.md](QUICK_START.md)** - Getting started

---

**Pro Tip**: For WASM apps >1MB, the Backup API is **essential** for good performance. Read [BACKUP_API.md](BACKUP_API.md)!
