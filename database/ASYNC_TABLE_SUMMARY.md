# Async Table Widget - Summary

Zero-lock ImGui table widget using double-buffering pattern for displaying frequently updated data.

## Key Features

✅ **Zero locks on render** - Only one atomic load  
✅ **ImGuiListClipper** - Efficient virtual scrolling  
✅ **Background updates** - Non-blocking refresh  
✅ **Simple API** - Define once, render every frame  
✅ **Customizable** - Column formatters, filters, styling  

## Quick Example

```cpp
#include "database/async_table_widget.h"

// Create widget
db::AsyncTableWidget table;
table.AddColumn("ID", 80.0f);
table.AddColumn("Name", 200.0f);
table.EnableFilter(true);

// Set refresh callback
table.SetRefreshCallback([](auto& rows) {
    // Query database in background
    auto results = repo.SelectAll();
    for (auto& r : results) {
        rows.push_back({std::to_string(r.id), r.name, r.status});
    }
});

// Initial load
table.Refresh();

// Start background thread (optional)
std::thread([]() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        table.Refresh(); // Thread-safe!
    }
}).detach();

// Render (GUI thread)
void OnGui() {
    table.Render(); // Zero locks!
}
```

## How It Works

1. **Double Buffer**: Two row buffers, one for reading (front), one for writing (back)
2. **Atomic Swap**: When back buffer is ready, atomically swap front index
3. **Zero Contention**: GUI reads front, background writes back - never conflict
4. **One-Frame Latency**: Acceptable for most UIs

## Performance

- **Render**: O(visible_rows) with ImGuiListClipper
- **Update**: O(n) for full refresh
- **Memory**: 2x row storage (minimal for string data)
- **Locks**: Zero on render path

## Pattern Origin

Based on ChatGPT discussion about ImGui best practices for high-frequency updates. 
See: [ASYNC_TABLE_EXAMPLES.md](ASYNC_TABLE_EXAMPLES.md) for detailed examples.

## Implementation

- **File**: [async_table_widget.h](async_table_widget.h) (9,300+ lines with docs)
- **Examples**: [ASYNC_TABLE_EXAMPLES.md](ASYNC_TABLE_EXAMPLES.md) (10,400+ lines)
- **Demo**: Integrated in [main.cpp](../main.cpp) 

## Use Cases

- Database result display (SELECT queries)
- Log viewers (streaming logs)
- Network state tables (connection status)
- Real-time metrics (performance counters)
- Any frequently updated list/table data

## See Also

- [ASYNC_TABLE_EXAMPLES.md](ASYNC_TABLE_EXAMPLES.md) - Complete usage examples
- [async_table_widget.h](async_table_widget.h) - Full API documentation
