# DatabaseManager Architecture

## Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         Application                              │
│                          (main.cpp)                              │
└────────────────┬────────────────────────────┬────────────────────┘
                 │                            │
                 │ owns                       │ owns
                 │                            │
                 ▼                            ▼
┌────────────────────────────┐  ┌────────────────────────────────┐
│    DatabaseManager         │  │    FooRepository (Demo)        │
│  (Connection Manager)      │◄─┤  (Table Operations)            │
├────────────────────────────┤  ├────────────────────────────────┤
│ + Initialize(config)       │  │ + CreateTable()                │
│ + GetConnection()          │  │ + Insert(id, name, fun)        │
│ + IsInitialized()          │  │ + SelectAll()                  │
│ + GetMode()                │  │ + SelectById(id)               │
│ + GetLastError()           │  │ + Update(id, name, fun)        │
│                            │  │ + Delete(id)                   │
│ - m_db: connection         │  │                                │
│ - m_currentMode: Mode      │  │ - m_db: DatabaseManager&       │
└────────────┬───────────────┘  └────────────┬───────────────────┘
             │                                │
             │ uses                           │ uses schema
             │                                │
             ▼                                ▼
┌────────────────────────────┐  ┌────────────────────────────────┐
│   DatabaseConfig           │  │   table_foo.h                  │
│                            │  │   (sqlpp23 schema)             │
├────────────────────────────┤  ├────────────────────────────────┤
│ + mode: DatabaseMode       │  │ namespace test_db {            │
│ + path: string             │  │   struct Foo_ {                │
│ + create_if_missing: bool  │  │     struct Id {...}            │
│                            │  │     struct Name {...}          │
│ Factory methods:           │  │     struct HasFun {...}        │
│ + Memory()                 │  │   };                           │
│ + NativeFile(path)         │  │   using Foo = table_t<Foo_>;   │
│ + OPFS(path)               │  │ }                              │
└────────────────────────────┘  └────────────────────────────────┘
             │
             │ uses
             ▼
┌────────────────────────────┐
│   DatabaseMode (enum)      │
├────────────────────────────┤
│ • Memory                   │
│ • NativeFile               │
│ • OPFS                     │
└────────────────────────────┘
```

## Data Flow

### Initialization Flow
```
main.cpp
   │
   ├─► DatabaseManager::Initialize(config)
   │      │
   │      ├─► Parse DatabaseMode
   │      ├─► Set connection path
   │      └─► Create sqlpp::sqlite3::connection
   │
   └─► FooRepository(dbManager)
          │
          └─► CreateTable() on connection
```

### Query Flow
```
FooRepository::SelectAll()
   │
   ├─► Get connection from DatabaseManager
   │      │
   │      └─► sqlpp::sqlite3::connection&
   │
   ├─► Execute sqlpp23 query
   │      │
   │      └─► select(all_of(foo)).from(foo)
   │
   └─► Return std::vector<FooRow>
```

## Separation of Concerns

### DatabaseManager (Infrastructure Layer)
**Responsibility**: Manage database connection lifecycle
- Connection creation
- Mode handling (Memory/File/OPFS)
- Error reporting
- Connection state management

**Does NOT**:
- Know about specific tables
- Perform business logic
- Handle table-specific operations

### FooRepository (Data Access Layer)
**Responsibility**: Table-specific data operations
- Table creation
- CRUD operations for "foo" table
- Query execution
- Data transformation

**Does NOT**:
- Manage connection lifecycle
- Know about other tables
- Handle connection modes

### Schemas (Data Definition Layer)
**Responsibility**: Type-safe table definitions
- sqlpp23 schema declarations
- Column definitions
- Type constraints

**Does NOT**:
- Contain business logic
- Know about repositories
- Manage connections

## Extension Points

### Adding New Tables

```
1. Create schema in database/schemas/
   └─► table_users.h (sqlpp23 definition)

2. Create repository in database/repositories/
   └─► user_repository.h
       ├─► Constructor takes DatabaseManager&
       ├─► Implement CRUD methods
       └─► Use schema from step 1

3. Use in application
   └─► UserRepository userRepo(g_dbManager);
```

### Supporting New Database Modes

```
1. Add to DatabaseMode enum
   └─► database_mode.h

2. Handle in DatabaseManager::Initialize()
   └─► database_manager.h
       └─► Add case for new mode

3. Add factory method to DatabaseConfig
   └─► DatabaseConfig::NewMode(...)
```

## Design Patterns Used

1. **Singleton Pattern**
   - DatabaseManager::Get() for global instance

2. **Repository Pattern**
   - FooRepository encapsulates data access
   - Separates business logic from data access

3. **Factory Method Pattern**
   - DatabaseConfig::Memory()
   - DatabaseConfig::NativeFile()
   - DatabaseConfig::OPFS()

4. **Strategy Pattern**
   - DatabaseMode enum for different storage strategies

## Dependencies

```
Application Layer (main.cpp)
        ↓
Data Access Layer (FooRepository)
        ↓
Infrastructure Layer (DatabaseManager)
        ↓
External Libraries (sqlpp23, SQLite3)
```

## Thread Safety

**Current Implementation**: Single-threaded
- DatabaseManager singleton is NOT thread-safe
- Repositories are NOT thread-safe
- SQLite connection is NOT shared across threads

**Future Considerations**:
- Add mutex for DatabaseManager singleton creation
- Connection pooling for multi-threaded access
- Per-thread connections for parallel queries

## Performance Characteristics

### Memory Mode
- **Speed**: Fastest (no disk I/O)
- **Persistence**: None
- **Use Case**: Testing, temporary data, caching

### Native File Mode
- **Speed**: Moderate (disk I/O)
- **Persistence**: Full (survives restarts)
- **Use Case**: Desktop applications, local storage

### OPFS Mode
- **Speed**: Fast (browser-optimized)
- **Persistence**: Per-origin (browser storage)
- **Use Case**: WebAssembly applications, offline-first PWAs
