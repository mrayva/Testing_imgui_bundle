# AsyncTableWidget v2 - Enhanced Features (2026-02-03)

Zero-lock ImGui table widget with **advanced native ImGui features**.

## What Changed?

### Original Design (v1)
- ✅ Double-buffering for zero-lock rendering
- ✅ ImGuiListClipper for efficiency
- ✅ Basic filter support
- ✅ Custom text formatters
- ❌ No sorting
- ❌ No icons
- ❌ No custom cell rendering beyond text

### Enhanced Design (v2)
- ✅ **Everything from v1** (backward compatible!)
- ✅ **Column Sorting** - Click headers to sort, auto-detect numeric vs string
- ✅ **Icons in Cells** - Show icon + text
- ✅ **Enum Formatters** - Map values to display text (e.g., "1" -> "Active")
- ✅ **Custom Cell Renderers** - Full control: colors, buttons, progress bars
- ✅ **Resizable Columns** - Drag borders to resize
- ✅ **Reorderable Columns** - Drag headers to reorder
- ✅ **Programmatic Sort** - Set sort from code
- ✅ **Fine Column Flags** - Per-column control

## Why Native ImGui Features Instead of ListViewBase?

**Decision**: Use native ImGui features instead of porting FiveM's `ImGui::ListViewBase`.

**Rationale**:
1. **ListViewBase is NOT standard** - It's a FiveM-specific custom extension
2. **Maintenance burden** - Would need to maintain custom widget code
3. **imgui_bundle compatibility** - Might conflict with bundle internals
4. **Native features are sufficient** - ImGui already provides everything we need
5. **Better documentation** - Standard ImGui features are well-documented

**Result**: Better solution with less code to maintain!

## New Features Overview

### 1. Column Sorting

**Enabled by default!** Just click column headers.

```cpp
db::AsyncTableWidget table;
table.AddColumn("ID", 80.0f);      // Auto-sorts numerically
table.AddColumn("Name", 200.0f);   // Auto-sorts alphabetically
table.AddColumn("Score", 100.0f);  // Auto-detects numbers

// User clicks header → widget sorts automatically
// Sorting happens during Refresh() (zero locks!)
```

**Features:**
- Auto-detects numeric columns (uses `strtod()`)
- Falls back to string comparison for text
- Sorts ascending/descending on click
- Shows sort arrow icons in headers
- **Zero locks** - sorting in back buffer!

**Disable if needed:**
```cpp
table.EnableSorting(false); // Disable entirely
// Or per-column:
table.AddColumn("Actions", 120.0f, ImGuiTableColumnFlags_NoSort);
```

### 2. Icons in Cells

Add icons before text in any column:

```cpp
ImTextureID icon = LoadTexture("file.png");
table.SetColumnIcon(0, icon, ImVec2(16, 16));

// Column 0 now shows: [ICON] text
```

### 3. Enum Formatters

Map raw values to display text:

```cpp
table.SetColumnEnumFormatter(2, [](const std::string& val) {
    if (val == "0") return "Inactive";
    if (val == "1") return "Active";
    return "Unknown";
});

// Database value "1" displays as "Active"
```

### 4. Custom Cell Renderers

Full control over cell rendering:

```cpp
// Colored status indicators
table.SetColumnCellRenderer(1, [](const Row& row, int col) {
    if (row.columns[col] == "ERROR") {
        ImGui::TextColored(ImVec4(1,0,0,1), "● ERROR");
    } else {
        ImGui::TextColored(ImVec4(0,1,0,1), "● OK");
    }
    return true; // We handled rendering
});

// Progress bars
table.SetColumnCellRenderer(3, [](const Row& row, int col) {
    float progress = std::stof(row.columns[col]) / 100.0f;
    ImGui::ProgressBar(progress, ImVec2(-1, 0));
    return true;
});

// Buttons
table.SetColumnCellRenderer(4, [](const Row& row, int col) {
    if (ImGui::SmallButton("Edit")) { /* handle */ }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) { /* handle */ }
    return true;
});
```

### 5. Resizable Columns

**Enabled by default!** Users can drag column borders.

```cpp
table.EnableResizing(true);  // Allow user resize
table.EnableResizing(false); // Lock column widths
```

### 6. Reorderable Columns

**Enabled by default!** Users can drag headers to reorder.

```cpp
table.EnableReordering(true);  // Allow drag-to-reorder
table.EnableReordering(false); // Lock column order
```

### 7. Programmatic Sort

Set sort from code:

```cpp
// Sort by column 2 (Date) descending
table.SetSort(2, ImGuiSortDirection_Descending);
table.Refresh(); // Apply

// Check current sort
int col = table.GetSortColumn();
auto dir = table.GetSortDirection();
```

### 8. Column Flags

Fine-grained control per column:

```cpp
table.AddColumn("ID", 80.0f, 
    ImGuiTableColumnFlags_WidthFixed | 
    ImGuiTableColumnFlags_DefaultSort |
    ImGuiTableColumnFlags_PreferSortAscending);

table.AddColumn("Actions", 120.0f,
    ImGuiTableColumnFlags_NoSort |
    ImGuiTableColumnFlags_NoResize |
    ImGuiTableColumnFlags_WidthFixed);
```

**Available flags:**
- `ImGuiTableColumnFlags_DefaultSort` - Sort this column by default
- `ImGuiTableColumnFlags_PreferSortAscending/Descending`
- `ImGuiTableColumnFlags_NoSort` - Disable sorting for this column
- `ImGuiTableColumnFlags_NoResize` - Lock width
- `ImGuiTableColumnFlags_WidthFixed` - Don't auto-size
- `ImGuiTableColumnFlags_WidthStretch` - Fill available space

## Zero-Lock Guarantee

**All features maintain zero-lock rendering!**

```
┌─────────────────────────────────────┐
│  Render() - GUI Thread              │
│  - Reads front buffer               │
│  - Zero locks!                      │
│  - Only atomic load                 │
└─────────────────────────────────────┘
                 │
                 │ atomic index
                 ▼
┌─────────────────────────────────────┐
│  Refresh() - Background Thread      │
│  - Writes to back buffer            │
│  - Queries database                 │
│  - Applies sorting                  │
│  - Atomic swap when done            │
└─────────────────────────────────────┘
```

**How it works:**
1. GUI calls `Render()` every frame → reads front buffer (zero locks!)
2. Background calls `Refresh()` → writes to back buffer
3. Sorting applied in back buffer before swap
4. Atomic swap publishes sorted data
5. GUI sees sorted data next frame

**Result**: Background thread never blocks GUI!

## API Reference

### New Methods

```cpp
// Column features
void SetColumnIcon(size_t col, ImTextureID texture, ImVec2 size = {16,16});
void SetColumnEnumFormatter(size_t col, std::function<string(const string&)> formatter);
void SetColumnCellRenderer(size_t col, CellRenderer renderer);

// Table features
void EnableSorting(bool enable = true);
void EnableResizing(bool enable = true);
void EnableReordering(bool enable = true);

// Sorting
void SetSort(int column, ImGuiSortDirection direction);
int GetSortColumn() const;
ImGuiSortDirection GetSortDirection() const;

// Flags
void SetTableFlags(ImGuiTableFlags flags);
ImGuiTableFlags GetTableFlags() const;
```

### Existing Methods (Unchanged)

```cpp
void AddColumn(const string& header, float width = 0.0f, 
               ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None,
               std::function<string(const Row&, int)> formatter = nullptr);
void SetRefreshCallback(std::function<void(vector<Row>&)> callback);
void Render();        // GUI thread - zero locks!
void Refresh();       // Background thread - safe
void EnableFilter(bool enable = true);
void SetData(vector<Row>&& rows);
void Clear();
```

## Backward Compatibility

**100% backward compatible!** Existing code works without changes.

```cpp
// Old code still works:
db::AsyncTableWidget table;
table.AddColumn("ID");
table.AddColumn("Name");
table.SetRefreshCallback([](auto& rows) { /* ... */ });
table.Render();

// New features are opt-in
table.EnableSorting(true);  // Now sortable!
table.SetColumnIcon(0, icon); // Add icon!
```

## Examples

See `ASYNC_TABLE_EXAMPLES.md` for:
- Example 7: Column sorting
- Example 8: Disable/enable sorting
- Example 9: Enum formatters
- Example 10: Icons in cells
- Example 11: Custom cell renderers
- Example 12: Resizable & reorderable
- Example 13: Programmatic sort
- Example 14: Complete showcase

## Performance

**No performance degradation!**

- Sorting: O(n log n) in back buffer (doesn't block GUI)
- Rendering: O(visible_rows) with ImGuiListClipper
- Memory: 2x row storage (same as before)
- **Zero locks on render path** (maintained!)

**Tested with:**
- ✅ Native build (gcc-14)
- ✅ Zero-lock guarantee verified
- ✅ Sorting logic working
- ⏳ WASM build (pending test)

## Migration Guide

### From v1 to v2

**No changes required!** Just rebuild.

**Optional enhancements:**

```cpp
// Before (v1):
table.AddColumn("Status");

// After (v2 - add enum formatter):
table.AddColumn("Status");
table.SetColumnEnumFormatter(2, [](const string& val) {
    return (val == "1") ? "Active" : "Inactive";
});

// Before (v1):
table.AddColumn("Name");

// After (v2 - make sortable):
// Already sortable by default! Just click header.

// Before (v1):
// (No icons supported)

// After (v2):
ImTextureID icon = LoadTexture("icon.png");
table.SetColumnIcon(0, icon);
```

## Comparison: ListViewBase vs Native ImGui

| Feature | ListViewBase (FiveM) | Native ImGui (Our Choice) |
|---------|---------------------|--------------------------|
| Sorting | ✅ Virtual methods | ✅ TableGetSortSpecs() |
| Icons | ✅ CellData::IconData | ✅ ImGui::Image() |
| Enums | ✅ HT_ENUM type | ✅ Enum formatters |
| Custom cells | ✅ Virtual getCellData | ✅ Cell renderers |
| **Standard** | ❌ FiveM-specific | ✅ Standard ImGui |
| **Maintenance** | ❌ Custom code | ✅ No extra code |
| **Documentation** | ❌ Limited | ✅ Well-documented |
| **Compatibility** | ❌ May conflict | ✅ Native support |

**Winner**: Native ImGui features! ✅

## Future Enhancements

Possible future additions (not yet implemented):
- Multi-column sort (ImGuiTableFlags_SortMulti)
- Column hiding (right-click menu)
- CSV export
- Column state persistence
- Row selection
- Context menus

## Credits

- **Pattern inspiration**: FiveM's ImGui::ListViewBase implementation
- **Decision analysis**: Research documented in session files
- **Implementation**: Native ImGui features (Dear ImGui + imgui_bundle)

## Links

- Examples: `database/ASYNC_TABLE_EXAMPLES.md`
- Summary: `database/ASYNC_TABLE_SUMMARY.md`
- Header: `database/async_table_widget.h`
- Decision doc: `~/.copilot/session-state/.../files/listview_analysis.md`

---

**Version 2 - Enhanced with native ImGui features**
**Zero-lock guarantee maintained!**
