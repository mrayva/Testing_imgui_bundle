# Async Table Widget - Usage Examples

Complete examples of using the AsyncTableWidget with sqlpp23 tables.

---

## Basic Example - Simple Display

```cpp
#include "database/async_table_widget.h"
#include "database/repositories/foo_repository.h"

// In your application:
db::AsyncTableWidget<test_db::foo> g_fooTable;

void InitializeUI() {
    // 1. Define columns
    g_fooTable.AddColumn("ID", 80.0f);
    g_fooTable.AddColumn("Name", 200.0f);
    g_fooTable.AddColumn("Has Fun", 100.0f);
    
    // 2. Set refresh callback
    g_fooTable.SetRefreshCallback([](auto& rows) {
        DatabaseManager& db = DatabaseManager::Get();
        FooRepository repo(db);
        
        // Query all rows
        auto results = repo.SelectAll();
        
        // Convert to widget rows
        for (const auto& result : results) {
            db::AsyncTableWidget<test_db::foo>::Row row(3);
            row.columns[0] = std::to_string(result.id);
            row.columns[1] = result.name;
            row.columns[2] = result.hasFun ? "Yes" : "No";
            rows.push_back(row);
        }
    });
    
    // 3. Enable filter (optional)
    g_fooTable.EnableFilter(true);
    
    // 4. Initial data load
    g_fooTable.Refresh();
}

void RenderUI() {
    ImGui::Begin("Foo Table");
    
    // Just render every frame - zero locks!
    g_fooTable.Render();
    
    // Optional: Manual refresh button
    if (ImGui::Button("Refresh Now")) {
        g_fooTable.Refresh();
    }
    
    ImGui::End();
}
```

---

## Advanced Example - Background Auto-Refresh

```cpp
#include <thread>
#include <atomic>
#include <chrono>

class AutoRefreshTable {
    db::AsyncTableWidget<test_db::foo> m_table;
    std::thread m_refreshThread;
    std::atomic<bool> m_running{false};
    std::chrono::milliseconds m_interval{1000}; // 1 second
    
public:
    void Start() {
        // Setup columns and callback
        m_table.AddColumn("ID", 80.0f);
        m_table.AddColumn("Name", 200.0f);
        m_table.AddColumn("Status", 100.0f, 
            ImGuiTableColumnFlags_None,
            [](const auto& row, int col) {
                // Custom formatter - color code status
                return row.columns[col] == "Active" ? "🟢 Active" : "🔴 Inactive";
            });
        
        m_table.SetRefreshCallback([](auto& rows) {
            DatabaseManager& db = DatabaseManager::Get();
            FooRepository repo(db);
            auto results = repo.SelectAll();
            
            for (const auto& r : results) {
                db::AsyncTableWidget<test_db::foo>::Row row(3);
                row.columns[0] = std::to_string(r.id);
                row.columns[1] = r.name;
                row.columns[2] = r.hasFun ? "Active" : "Inactive";
                rows.push_back(row);
            }
        });
        
        // Initial load
        m_table.Refresh();
        
        // Start background refresh thread
        m_running = true;
        m_refreshThread = std::thread([this]() {
            while (m_running) {
                std::this_thread::sleep_for(m_interval);
                m_table.Refresh(); // Thread-safe!
            }
        });
    }
    
    void Stop() {
        m_running = false;
        if (m_refreshThread.joinable()) {
            m_refreshThread.join();
        }
    }
    
    void Render() {
        m_table.Render();
        
        // Show refresh interval control
        int ms = m_interval.count();
        if (ImGui::SliderInt("Refresh Interval (ms)", &ms, 100, 5000)) {
            m_interval = std::chrono::milliseconds(ms);
        }
    }
    
    ~AutoRefreshTable() {
        Stop();
    }
};
```

---

## Example - Multiple Tables

```cpp
class DatabaseDashboard {
    db::AsyncTableWidget<test_db::foo> m_fooTable;
    db::AsyncTableWidget<test_db::users> m_usersTable;
    db::AsyncTableWidget<test_db::logs> m_logsTable;
    
public:
    void Initialize() {
        // Setup Foo table
        m_fooTable.AddColumn("ID", 80.0f);
        m_fooTable.AddColumn("Name", 200.0f);
        m_fooTable.SetRefreshCallback([](auto& rows) {
            // ... populate from FooRepository
        });
        
        // Setup Users table
        m_usersTable.AddColumn("User ID", 80.0f);
        m_usersTable.AddColumn("Username", 150.0f);
        m_usersTable.AddColumn("Email", 200.0f);
        m_usersTable.SetRefreshCallback([](auto& rows) {
            // ... populate from UserRepository
        });
        
        // Setup Logs table (recent entries only)
        m_logsTable.AddColumn("Time", 150.0f);
        m_logsTable.AddColumn("Level", 80.0f);
        m_logsTable.AddColumn("Message", 300.0f);
        m_logsTable.EnableFilter(true); // Searchable logs
        m_logsTable.SetRefreshCallback([](auto& rows) {
            // ... populate from LogRepository
        });
    }
    
    void Render() {
        if (ImGui::BeginTabBar("DatabaseTabs")) {
            if (ImGui::BeginTabItem("Foo Data")) {
                m_fooTable.Render();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Users")) {
                m_usersTable.Render();
                ImGui::EndTabItem();
            }
            
            if (ImGui::BeginTabItem("Logs")) {
                m_logsTable.Render();
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        // Refresh all button
        if (ImGui::Button("Refresh All Tables")) {
            std::thread([this]() {
                m_fooTable.Refresh();
                m_usersTable.Refresh();
                m_logsTable.Refresh();
            }).detach();
        }
    }
};
```

---

## Example - With Custom Formatters

```cpp
void SetupTableWithFormatters() {
    static db::AsyncTableWidget<test_db::foo> table;
    
    // Column 0: ID with padding
    table.AddColumn("ID", 80.0f, ImGuiTableColumnFlags_None,
        [](const auto& row, int col) {
            return "  " + row.columns[col]; // Add padding
        });
    
    // Column 1: Name in uppercase
    table.AddColumn("Name", 200.0f, ImGuiTableColumnFlags_None,
        [](const auto& row, int col) {
            std::string name = row.columns[col];
            std::transform(name.begin(), name.end(), name.begin(), ::toupper);
            return name;
        });
    
    // Column 2: Boolean as emoji
    table.AddColumn("Status", 100.0f, ImGuiTableColumnFlags_None,
        [](const auto& row, int col) {
            return row.columns[col] == "true" ? "✅" : "❌";
        });
    
    // Column 3: Computed field (not from DB)
    table.AddColumn("Actions", 150.0f, ImGuiTableColumnFlags_None,
        [](const auto& row, int col) {
            return "[Edit] [Delete]";
        });
    
    table.SetRefreshCallback([](auto& rows) {
        // ... populate
    });
}
```

---

## Example - Integration with main.cpp

```cpp
// In main.cpp:
#include "database/async_table_widget.h"

// Global or static
static db::AsyncTableWidget<test_db::foo>* g_asyncFooTable = nullptr;
static std::thread g_refreshThread;
static std::atomic<bool> g_running{false};

void Gui() {
    // ... existing UI code ...
    
    // New: Async Table Demo
    if (ImGui::CollapsingHeader("Async Table (Zero-Lock)")) {
        if (g_asyncFooTable) {
            g_asyncFooTable->Render();
            
            ImGui::SameLine();
            if (ImGui::Button("Manual Refresh")) {
                // Safe to call from any thread!
                g_asyncFooTable->Refresh();
            }
            
            ImGui::Text("Row count: %zu", g_asyncFooTable->GetRowCount());
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Table not initialized");
        }
    }
}

int main(int, char**) {
    // ... database initialization ...
    
    // Setup async table
    g_asyncFooTable = new db::AsyncTableWidget<test_db::foo>();
    g_asyncFooTable->AddColumn("ID", 80.0f);
    g_asyncFooTable->AddColumn("Name", 200.0f);
    g_asyncFooTable->AddColumn("Has Fun", 100.0f);
    g_asyncFooTable->EnableFilter(true);
    
    g_asyncFooTable->SetRefreshCallback([](auto& rows) {
        DatabaseManager& db = DatabaseManager::Get();
        FooRepository repo(db);
        auto results = repo.SelectAll();
        
        for (const auto& r : results) {
            db::AsyncTableWidget<test_db::foo>::Row row(3);
            row.columns[0] = std::to_string(r.id);
            row.columns[1] = r.name;
            row.columns[2] = r.hasFun ? "Yes" : "No";
            rows.push_back(row);
        }
    });
    
    // Initial load
    g_asyncFooTable->Refresh();
    
    // Start background refresh (every 2 seconds)
    g_running = true;
    g_refreshThread = std::thread([]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (g_asyncFooTable) {
                g_asyncFooTable->Refresh();
            }
        }
    });
    
    // Run app
    // ... ImmApp::Run(...) ...
    
    // Cleanup
    g_running = false;
    if (g_refreshThread.joinable()) {
        g_refreshThread.join();
    }
    delete g_asyncFooTable;
    
    return 0;
}
```

---

## Performance Characteristics

### Render Path (GUI Thread)
- ✅ **Zero locks** - only one atomic load
- ✅ **Consistent snapshot** - never sees partial updates
- ✅ **ImGuiListClipper** - only renders visible rows
- ✅ **Cache-friendly** - sequential reads

### Update Path (Background Thread)
- ✅ **Non-blocking** - never waits for GUI
- ✅ **One atomic store** - minimal overhead
- ✅ **Writer never blocks** - always has a back buffer
- ⚠️ **One-frame latency** - acceptable for most UIs

### Memory
- 2x row storage (double buffer)
- Small if using virtual scrolling
- Strings are cheap to copy for small datasets

---

## Best Practices

1. **Keep row data simple** - Strings are easy to display/filter
2. **Use formatters for presentation** - Don't store formatted data
3. **Filter in render, not refresh** - Keep data raw
4. **One writer per table** - Or use external sync
5. **Initial load before first render** - Call Refresh() in setup
6. **Detach refresh threads** - Or manage lifetime carefully

---

## See Also

- [database/async_table_widget.h](async_table_widget.h) - Widget implementation
- [database/repositories/foo_repository.h](repositories/foo_repository.h) - Example repository
- [database/EXAMPLES.md](EXAMPLES.md) - More database examples
