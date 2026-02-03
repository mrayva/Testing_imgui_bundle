#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <any>
#include <algorithm>
#include "imgui.h"
#include "database_manager.h"

namespace db {

/**
 * @brief Async table widget for ImGui with zero-lock rendering
 * 
 * Uses TYPE ERASURE pattern with double-buffering to display database
 * query results that can be updated in background threads without blocking the UI.
 * 
 * MANDATORY: All rows MUST have typed data in userData for sorting to work.
 * 
 * Features:
 * - Zero locks on render path (atomic swap only)
 * - Type-safe sorting via typedExtractor (no FooRow structs needed!)
 * - ImGuiListClipper for efficient large lists
 * - Customizable column formatters and renderers
 * - Background refresh support
 * 
 * Example (sqlpp23 integration):
 *   struct FooData { int64_t id; std::string name; bool active; };
 *   
 *   AsyncTableWidget widget;
 *   widget.AddColumn("ID", 0.3f, ImGuiTableColumnFlags_DefaultSort);
 *   widget.AddColumn("Name");
 *   widget.AddColumn("Active");
 *   
 *   widget.SetRefreshCallback([&](auto& rows) {
 *       auto results = db(select(all_of(foo)).from(foo));
 *       for (const auto& sqlppRow : results) {
 *           int64_t id = sqlppRow.Id;
 *           FooData data{id, sqlppRow.Name, sqlppRow.HasFun};
 *           rows.push_back({
 *               .columns = {std::to_string(id), data.name, data.active ? "Yes" : "No"},
 *               .userData = data  // Store typed data!
 *           });
 *       }
 *   });
 *   
 *   // Type-safe sorting on ID column
 *   widget.SetColumnTypedExtractor(0, [](const Row& row) {
 *       auto& data = std::any_cast<const FooData&>(row.userData);
 *       return std::any(data.id);  // Sorts as int64, not string!
 *   });
 *   
 *   // GUI thread:
 *   widget.Render();
 *   
 *   // Background thread:
 *   widget.Refresh(); // Can be called from worker thread
 */
class AsyncTableWidget {
public:
    /**
     * @brief Row type - stores extracted column values
     * 
     * This is a generic representation that sqlpp23 query results
     * get converted into. Keeps the render path independent of
     * database types.
     * 
     * NEW: Now supports type erasure! Store the original typed sqlpp23 row
     * in userData for type-safe operations (sorting, custom formatters).
     */
    struct Row {
        // Raw column data as strings (easy to display, compare, filter)
        std::vector<std::string> columns;
        
        // Optional: Store original typed row (e.g., sqlpp23 result row)
        // This enables type-safe sorting and formatters without FooRow structs!
        std::any userData;
        
        Row() = default;
        
        explicit Row(size_t numColumns) : columns(numColumns) {}
        
        Row(std::initializer_list<std::string> cols) : columns(cols) {}
        
        // NEW: Constructor that stores both strings and typed data
        Row(std::initializer_list<std::string> cols, std::any typedData) 
            : columns(cols), userData(typedData) {}
    };
    
    /**
     * @brief Cell render function type
     * Return true if custom rendering was done, false to use default
     */
    using CellRenderer = std::function<bool(const Row&, int colIndex)>;
    
    /**
     * @brief Typed value extractor - extracts typed value from Row.userData
     * REQUIRED for sortable columns to enable type-safe sorting.
     * 
     * Example:
     *   [](const Row& row) -> std::any {
     *       auto& data = std::any_cast<const MyData&>(row.userData);
     *       return std::any(data.id);  // Extract int64_t field
     *   }
     */
    using TypedExtractor = std::function<std::any(const Row&)>;
    
    /**
     * @brief Column configuration with advanced features
     */
    struct ColumnConfig {
        std::string header;
        float width = 0.0f;  // 0 = auto-size
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None;
        
        // Optional: Custom text formatter (accesses Row.userData if needed)
        std::function<std::string(const Row&, int colIndex)> formatter;
        
        // Optional: Custom cell renderer (for icons, colors, buttons, etc.)
        // If returns true, default text rendering is skipped
        CellRenderer cellRenderer;
        
        // REQUIRED for sortable columns: Typed value extractor
        // Extracts typed values from Row.userData for accurate sorting
        TypedExtractor typedExtractor;
        
        // Optional: Icon texture for this column's cells
        ImTextureID iconTexture = 0;  // nullptr is not valid for ImTextureID (it's unsigned long long)
        ImVec2 iconSize = ImVec2(16, 16);
        
        // Optional: Enum converter (maps string value to display text)
        std::function<std::string(const std::string&)> enumFormatter;
        
        ColumnConfig(const std::string& h, float w = 0.0f) 
            : header(h), width(w) {}
    };

private:
    // Double buffer for rows
    std::vector<Row> m_buffers[2];
    std::atomic<int> m_frontIndex{0};
    
    // Column definitions
    std::vector<ColumnConfig> m_columns;
    
    // Refresh callback (called on background thread)
    std::function<void(std::vector<Row>&)> m_refreshCallback;
    
    // ImGui table state
    std::string m_tableId;
    ImGuiTableFlags m_tableFlags = ImGuiTableFlags_Borders | 
                                   ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_Sortable |
                                   ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_Reorderable;
    
    // Optional: Filter/search
    char m_filterBuffer[256] = "";
    bool m_filterEnabled = false;
    
    // Sorting state
    int m_lastSortColumn = -1;
    ImGuiSortDirection m_lastSortDirection = ImGuiSortDirection_None;
    
    // Comparison function for sorting (can be customized)
    using RowComparator = std::function<bool(const Row&, const Row&, int column, bool ascending)>;

public:
    AsyncTableWidget() {
        // Generate unique table ID
        static int s_tableCounter = 0;
        m_tableId = "AsyncTable##" + std::to_string(s_tableCounter++);
    }
    
    /**
     * @brief Add a column to the table
     * 
     * @param header Column header text
     * @param width Column width (0 = auto)
     * @param flags ImGui column flags (includes sorting, resizing options)
     * @param formatter Optional custom formatter function
     */
    void AddColumn(
        const std::string& header,
        float width = 0.0f,
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None,
        std::function<std::string(const Row&, int)> formatter = nullptr
    ) {
        ColumnConfig col(header, width);
        col.flags = flags;
        col.formatter = formatter;
        m_columns.push_back(col);
    }
    
    /**
     * @brief Set the refresh callback
     * 
     * This callback is executed on the background thread and should
     * populate the provided vector with fresh data.
     * 
     * @param callback Function that fills vector with updated rows
     */
    void SetRefreshCallback(std::function<void(std::vector<Row>&)> callback) {
        m_refreshCallback = callback;
    }
    
    /**
     * @brief Set typed extractor for a column (enables type-safe sorting)
     * 
     * Example with sqlpp23:
     *   widget.SetColumnTypedExtractor(0, [](const Row& row) -> std::any {
     *       if (!row.userData.has_value()) return {};
     *       auto& sqlppRow = std::any_cast<const MyResultRow&>(row.userData);
     *       return std::any(static_cast<int64_t>(sqlppRow.Id));
     *   });
     * 
     * When a typed extractor is set, sorting uses the extracted typed value
     * instead of comparing strings. This gives accurate numeric sorting!
     */
    void SetColumnTypedExtractor(size_t colIndex, TypedExtractor extractor) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].typedExtractor = extractor;
        }
    }
    
    /**
     * @brief Set ImGui table flags
     */
    void SetTableFlags(ImGuiTableFlags flags) {
        m_tableFlags = flags;
    }
    
    /**
     * @brief Enable search/filter bar
     */
    void EnableFilter(bool enable = true) {
        m_filterEnabled = enable;
    }
    
    /**
     * @brief Render the table (call every frame from GUI thread)
     * 
     * This is LOCK-FREE - only reads the front buffer via atomic load.
     * Never blocks, even if background refresh is running.
     */
    void Render() {
        // Atomic read (acquire semantics)
        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        const std::vector<Row>& rows = m_buffers[frontIdx];
        
        // Optional filter bar
        if (m_filterEnabled) {
            ImGui::InputText("Filter", m_filterBuffer, sizeof(m_filterBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                m_filterBuffer[0] = '\0';
            }
        }
        
        // Show row count
        ImGui::Text("%zu rows", rows.size());
        
        // Render table
        if (ImGui::BeginTable(m_tableId.c_str(), m_columns.size(), m_tableFlags)) {
            // Setup columns
            for (const auto& col : m_columns) {
                ImGui::TableSetupColumn(col.header.c_str(), col.flags, col.width);
            }
            ImGui::TableHeadersRow();
            
            // Check for sorting changes
            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
                    // Get first sort spec (support single-column sort for now)
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    m_lastSortColumn = spec.ColumnIndex;
                    m_lastSortDirection = spec.SortDirection;
                    
                    // NOTE: We'll sort the back buffer during Refresh()
                    // to maintain zero-lock rendering. Just mark it dirty here.
                    sortSpecs->SpecsDirty = false;
                }
            }
            
            // Use ImGuiListClipper for efficient rendering of large lists
            ImGuiListClipper clipper;
            clipper.Begin(rows.size());
            
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const Row& rowData = rows[row];
                    
                    // Apply filter
                    if (m_filterEnabled && m_filterBuffer[0] != '\0') {
                        bool matches = false;
                        for (const auto& col : rowData.columns) {
                            if (col.find(m_filterBuffer) != std::string::npos) {
                                matches = true;
                                break;
                            }
                        }
                        if (!matches) continue;
                    }
                    
                    ImGui::TableNextRow();
                    
                    // Render cells
                    for (size_t col = 0; col < m_columns.size() && col < rowData.columns.size(); col++) {
                        ImGui::TableSetColumnIndex(col);
                        
                        const auto& colCfg = m_columns[col];
                        bool rendered = false;
                        
                        // Try custom cell renderer first
                        if (colCfg.cellRenderer) {
                            rendered = colCfg.cellRenderer(rowData, col);
                        }
                        
                        // If not custom rendered, try icon + text
                        if (!rendered && colCfg.iconTexture) {
                            ImGui::Image(colCfg.iconTexture, colCfg.iconSize);
                            ImGui::SameLine();
                        }
                        
                        // Render text (unless custom renderer handled it)
                        if (!rendered) {
                            std::string text;
                            
                            // Use enum formatter if available
                            if (colCfg.enumFormatter) {
                                text = colCfg.enumFormatter(rowData.columns[col]);
                            }
                            // Use custom formatter if provided
                            else if (colCfg.formatter) {
                                text = colCfg.formatter(rowData, col);
                            }
                            // Default: use raw string
                            else {
                                text = rowData.columns[col];
                            }
                            
                            ImGui::TextUnformatted(text.c_str());
                        }
                    }
                }
            }
            
            ImGui::EndTable();
        }
    }
    
    /**
     * @brief Refresh data in background (safe to call from any thread)
     * 
     * This writes to the BACK buffer and atomically swaps when done.
     * The GUI thread never sees partial updates.
     * 
     * Important: Only ONE thread should call this at a time, OR
     * you need external synchronization between multiple writers.
     * 
     * Sorting is applied here (not in Render) to maintain zero-lock rendering.
     */
    void Refresh() {
        if (!m_refreshCallback) {
            return; // No refresh callback set
        }
        
        // Determine back buffer index (relaxed is fine - we're the only writer)
        int currentFront = m_frontIndex.load(std::memory_order_relaxed);
        int backIdx = 1 - currentFront;
        
        // Clear and populate back buffer
        std::vector<Row>& backBuffer = m_buffers[backIdx];
        backBuffer.clear();
        
        // Execute refresh callback (this is where the DB query happens)
        m_refreshCallback(backBuffer);
        
        // Apply sorting if requested (TYPED SORTING ONLY!)
        if (m_lastSortColumn >= 0 && m_lastSortColumn < (int)m_columns.size()) {
            bool ascending = (m_lastSortDirection == ImGuiSortDirection_Ascending);
            const auto& colCfg = m_columns[m_lastSortColumn];
            
            // Typed extractor is REQUIRED for sorting
            if (!colCfg.typedExtractor) {
                // Column marked sortable but no typed extractor - log error
                std::cerr << "ERROR: Column '" << colCfg.header 
                          << "' is sortable but has no typedExtractor. Skipping sort.\n";
            } else {
                std::sort(backBuffer.begin(), backBuffer.end(),
                    [&colCfg, ascending](const Row& a, const Row& b) {
                        // Extract typed values from userData
                        std::any aVal = colCfg.typedExtractor(a);
                        std::any bVal = colCfg.typedExtractor(b);
                        
                        if (!aVal.has_value() || !bVal.has_value()) {
                            return false;  // Can't compare empty values
                        }
                        
                        // Type-safe comparison (try common types)
                        if (auto* av = std::any_cast<int64_t>(&aVal)) {
                            auto bv = std::any_cast<int64_t>(bVal);
                            return ascending ? (*av < bv) : (*av > bv);
                        } else if (auto* av = std::any_cast<int>(&aVal)) {
                            auto bv = std::any_cast<int>(bVal);
                            return ascending ? (*av < bv) : (*av > bv);
                        } else if (auto* av = std::any_cast<double>(&aVal)) {
                            auto bv = std::any_cast<double>(bVal);
                            return ascending ? (*av < bv) : (*av > bv);
                        } else if (auto* av = std::any_cast<bool>(&aVal)) {
                            auto bv = std::any_cast<bool>(bVal);
                            return ascending ? (*av < bv) : (*av > bv);
                        } else if (auto* av = std::any_cast<std::string>(&aVal)) {
                            auto bv = std::any_cast<std::string>(bVal);
                            return ascending ? (*av < bv) : (*av > bv);
                        } else if (auto* av = std::any_cast<std::string_view>(&aVal)) {
                            auto bv = std::any_cast<std::string_view>(bVal);
                            return ascending ? (*av < bv) : (*av > bv);
                        }
                        
                        // Unknown type - can't sort
                        std::cerr << "WARNING: Unknown type in typedExtractor, cannot sort.\n";
                        return false;
                    });
            }
        }
        
        // Atomic swap (release semantics - ensures all writes are visible)
        m_frontIndex.store(backIdx, std::memory_order_release);
    }
    
    /**
     * @brief Manually set data (useful for initial population or testing)
     */
    void SetData(std::vector<Row>&& rows) {
        int currentFront = m_frontIndex.load(std::memory_order_relaxed);
        int backIdx = 1 - currentFront;
        m_buffers[backIdx] = std::move(rows);
        m_frontIndex.store(backIdx, std::memory_order_release);
    }
    
    /**
     * @brief Get current row count (from front buffer)
     */
    size_t GetRowCount() const {
        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        return m_buffers[frontIdx].size();
    }
    
    /**
     * @brief Clear all data
     */
    void Clear() {
        int currentFront = m_frontIndex.load(std::memory_order_relaxed);
        int backIdx = 1 - currentFront;
        m_buffers[backIdx].clear();
        m_frontIndex.store(backIdx, std::memory_order_release);
    }
    
    // ==================== Advanced Features Helpers ====================
    
    /**
     * @brief Get current table flags
     */
    ImGuiTableFlags GetTableFlags() const {
        return m_tableFlags;
    }
    
    /**
     * @brief Enable/disable specific table features
     */
    void EnableSorting(bool enable = true) {
        if (enable) {
            m_tableFlags |= ImGuiTableFlags_Sortable;
        } else {
            m_tableFlags &= ~ImGuiTableFlags_Sortable;
        }
    }
    
    void EnableResizing(bool enable = true) {
        if (enable) {
            m_tableFlags |= ImGuiTableFlags_Resizable;
        } else {
            m_tableFlags &= ~ImGuiTableFlags_Resizable;
        }
    }
    
    void EnableReordering(bool enable = true) {
        if (enable) {
            m_tableFlags |= ImGuiTableFlags_Reorderable;
        } else {
            m_tableFlags &= ~ImGuiTableFlags_Reorderable;
        }
    }
    
    /**
     * @brief Set icon texture for a specific column
     * @param colIndex Column index
     * @param texture ImTextureID (nullptr to remove icon)
     * @param size Icon size in pixels
     */
    void SetColumnIcon(size_t colIndex, ImTextureID texture, ImVec2 size = ImVec2(16, 16)) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].iconTexture = texture;
            m_columns[colIndex].iconSize = size;
        }
    }
    
    /**
     * @brief Set enum formatter for a column (maps values to display text)
     * Example: enum {"0" -> "Inactive", "1" -> "Active"}
     */
    void SetColumnEnumFormatter(size_t colIndex, 
                                std::function<std::string(const std::string&)> formatter) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].enumFormatter = formatter;
        }
    }
    
    /**
     * @brief Set custom cell renderer for a column
     * Return true from renderer if you handled rendering, false to use default
     */
    void SetColumnCellRenderer(size_t colIndex, CellRenderer renderer) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].cellRenderer = renderer;
        }
    }
    
    /**
     * @brief Get current sort state
     */
    int GetSortColumn() const { return m_lastSortColumn; }
    ImGuiSortDirection GetSortDirection() const { return m_lastSortDirection; }
    
    /**
     * @brief Programmatically set sort (will apply on next Refresh)
     */
    void SetSort(int column, ImGuiSortDirection direction) {
        m_lastSortColumn = column;
        m_lastSortDirection = direction;
    }
};

} // namespace db
