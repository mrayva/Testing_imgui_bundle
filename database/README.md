# Database Module

Refactored database management system with support for multiple storage modes.

## Structure

```
database/
├── database_mode.h          # DatabaseMode enum and DatabaseConfig
├── database_manager.h       # Generic connection manager (no table dependencies)
├── schemas/
│   └── table_foo.h         # sqlpp23 schema definitions
└── repositories/
    └── foo_repository.h    # Table-specific CRUD operations (demo)
```

## Usage

### 1. Initialize Database

```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"

// Memory mode (default)
DatabaseManager& db = DatabaseManager::Get();
db.Initialize(DatabaseConfig::Memory());

// Native file mode
db.Initialize(DatabaseConfig::NativeFile("/path/to/database.db"));

// OPFS mode (WebAssembly only)
db.Initialize(DatabaseConfig::OPFS("/opfs/app.db"));
```

### 2. Use Repositories

```cpp
// Create repository for demo table
FooRepository fooRepo(db);

// Create table
fooRepo.CreateTable();

// Insert data
fooRepo.Insert(1, "User Name", true);

// Query data
auto rows = fooRepo.SelectAll();
for (const auto& row : rows) {
    std::cout << row.id << ": " << row.name << std::endl;
}

// Select by ID
auto row = fooRepo.SelectById(1);
if (row) {
    std::cout << "Found: " << row->name << std::endl;
}
```

### 3. Custom Tables

Create your own repositories following the `FooRepository` pattern:

```cpp
class UserRepository {
public:
    explicit UserRepository(DatabaseManager& db) : m_db(db) {}
    
    bool CreateTable() {
        m_db.GetConnection()("CREATE TABLE users (...)");
    }
    
    // Add your CRUD methods...
    
private:
    DatabaseManager& m_db;
};
```

## Database Modes

### Memory Mode
- Pure in-memory database
- Fast, no persistence
- Data lost when application closes
- Uses SQLite `:memory:` special filename

### Native File Mode  
- Disk-based SQLite file
- Available in native builds only
- Persistent across restarts
- Standard file I/O

### OPFS Mode (WebAssembly)
- Origin-Private FileSystem
- Browser-side persistence
- Requires security headers:
  - `Cross-Origin-Opener-Policy: same-origin`
  - `Cross-Origin-Embedder-Policy: require-corp`
- May require Web Worker for synchronous I/O
- Performance comparable to native disk
- **Automatic performance tuning applied** (16MB cache, WAL journal)

## Notes

- Requires sqlpp23 for type-safe SQL
- Compiled with gcc-14 (gcc-15 has "deducing this" bug)
- C++23 standard required
- `FooRepository` is just a demo - create your own repositories as needed
- **Automatic performance tuning** applied based on database mode
  - OPFS: 16MB cache for optimal browser performance
  - Native: 8MB cache with WAL journaling
  - Memory: Aggressive settings for maximum speed
