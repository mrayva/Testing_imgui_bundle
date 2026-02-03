# Typed AsyncTableWidget - Implementation Notes

## Goal
Work exclusively with sqlpp23 types, avoiding intermediate structs like `FooRow`.

## Challenge
sqlpp23 result types are complex templates that depend on the query. The row type is:
```cpp
sqlpp::result_row_t<field_spec1, field_spec2, ...>
```

This type is only known after executing a query, making it difficult to declare widgets beforehand.

## Solution Options

### Option 1: Template Widget (Current Attempt - Complex)
```cpp
template<typename RowType>
class AsyncTableWidgetTyped {
    std::vector<RowType> m_buffers[2];
    ...
};

// Problem: RowType is query-dependent, can't declare static widget
static AsyncTableWidgetTyped<???> widget; // What type goes here?
```

### Option 2: Type Erasure with std::any (RECOMMENDED)
Keep widget non-templated, but store typed extractors:
```cpp
class AsyncTableWidget {
    struct Row {
        std::any typedData;  // Store original sqlpp23 row
        std::vector<std::string> cachedColumns;  // For filter/render
    };
};

// Usage:
widget.SetRefreshCallback([](auto& rows) {
    for (const auto& sqlppRow : db(...)) {
        Row r;
        r.typedData = sqlppRow;  // Store typed
        r.cachedColumns = {to_string(sqlppRow.Id), ...};  // Cache strings
        rows.push_back(r);
    }
});
```

### Option 3: Helper Functions (SIMPLEST)
Keep current string-based widget, but add helpers to reduce boilerplate:
```cpp
namespace db {
    template<typename SqlppRow>
    std::vector<AsyncTableWidget::Row> ConvertSqlppRows(const auto& results) {
        std::vector<AsyncTableWidget::Row> rows;
        for (const auto& r : results) {
            // Extract all columns automatically via reflection
            rows.push_back(...);
        }
        return rows;
    }
}
```

## Recommendation

Use **Option 2** (Type Erasure):
- Modify existing `AsyncTableWidget::Row` to include `std::any typedData`
- Keep cached string columns for filtering
- Extractors and formatters access `typedData` for sorting/display
- Widget remains non-templated (easier to declare)
- Full type safety where it matters (sorting, custom renderers)

## Next Step

Update `AsyncTableWidget` to use type erasure approach instead of full templating.
