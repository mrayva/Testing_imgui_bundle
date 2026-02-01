# Database Module Documentation Index

## Quick Navigation

### Getting Started
0. **[QUICK_START.md](QUICK_START.md)** - 5-minute setup guide ⚡
   - Fastest way to get started
   - Common patterns
   - Troubleshooting

1. **[README.md](README.md)** - Usage guide and overview
   - Introduction to the database module
   - Basic usage examples
   - Database modes explained

2. **[EXAMPLES.md](EXAMPLES.md)** - Practical code examples
   - Quick start for each mode
   - CRUD operations
   - GUI integration
   - Performance tuning

### For Existing Users
3. **[MIGRATION.md](MIGRATION.md)** - Upgrading from old API
   - What changed
   - Step-by-step migration guide
   - Side-by-side API comparison
   - Complete migration example

### Understanding the Design
4. **[ARCHITECTURE.md](ARCHITECTURE.md)** - System design
   - Component diagrams
   - Data flow visualization
   - Separation of concerns
   - Extension points
   - Design patterns used

5. **[PERFORMANCE.md](PERFORMANCE.md)** - OPFS performance tuning
   - Why tuning matters (10-100x speedup!)
   - Mode-specific defaults
   - PRAGMA deep dive
   - Benchmarking guide
   - Troubleshooting

## File Structure

```
database/
├── INDEX.md                    ← You are here
├── README.md                   ← Start here for basic usage
├── MIGRATION.md                ← For upgrading existing code
├── EXAMPLES.md                 ← Copy-paste examples
├── ARCHITECTURE.md             ← Design documentation
├── PERFORMANCE.md              ← OPFS tuning guide (NEW!)
│
├── database_mode.h             ← Mode enum + config + tuning
├── database_manager.h          ← Connection manager
│
├── schemas/
│   └── table_foo.h            ← sqlpp23 table definitions
│
└── repositories/
    └── foo_repository.h       ← Table CRUD operations
```

## Common Tasks

### I want to...

**Use the database in my application**
→ Start with [README.md](README.md) section "Usage"

**See working code examples**
→ Check [EXAMPLES.md](EXAMPLES.md)

**Upgrade from old DatabaseManager**
→ Follow [MIGRATION.md](MIGRATION.md)

**Understand the architecture**
→ Read [ARCHITECTURE.md](ARCHITECTURE.md)

**Add a new table**
→ See [ARCHITECTURE.md](ARCHITECTURE.md) section "Extension Points"

**Switch from memory to file storage**
→ See [EXAMPLES.md](EXAMPLES.md) section "Quick Start"

**Use OPFS in WebAssembly**
→ See [README.md](README.md) section "OPFS Mode"

**Create my own repository**
→ See [EXAMPLES.md](EXAMPLES.md) section "Multiple Tables"

**Optimize OPFS performance**
→ Read [PERFORMANCE.md](PERFORMANCE.md) - **10-100x speedup!**

**Understand performance tuning**
→ See [PERFORMANCE.md](PERFORMANCE.md) PRAGMA guide

## Key Concepts

### Three Database Modes
- **Memory**: Fast, no persistence (`:memory:`)
- **NativeFile**: Disk-based, persistent
- **OPFS**: Browser storage, WebAssembly only

### Two Main Components
- **DatabaseManager**: Manages connection (generic)
- **Repository**: Manages table operations (table-specific)

### Clean Separation
- Manager knows nothing about tables
- Repositories know nothing about connection lifecycle
- Easy to add new tables without touching manager

## Quick Example

```cpp
#include "database/database_manager.h"
#include "database/repositories/foo_repository.h"

// Initialize
DatabaseManager& db = DatabaseManager::Get();
db.Initialize(DatabaseConfig::Memory());

// Use repository
FooRepository repo(db);
repo.CreateTable();
repo.Insert(1, "Hello", true);
auto rows = repo.SelectAll();
```

## Additional Resources

- **Root Summary**: `../DATABASE_REFACTOR_SUMMARY.md`
- **Verification Script**: `../verify_refactor.sh`
- **Main Application**: `../main.cpp` (see how it's integrated)

## Need Help?

1. Check [EXAMPLES.md](EXAMPLES.md) for your specific use case
2. Review [MIGRATION.md](MIGRATION.md) if upgrading
3. Read [ARCHITECTURE.md](ARCHITECTURE.md) to understand design decisions

## Version Info

- **Compiler**: gcc-14 (required due to gcc-15 sqlpp23 bug)
- **Standard**: C++23
- **Dependencies**: sqlpp23, SQLite3
