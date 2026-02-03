#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <algorithm>
#include <type_traits>
#include "imgui.h"
#include "database_manager.h"

namespace db {

/**
 * @brief Async table widget for ImGui with zero-lock rendering (TYPED VERSION)
 * 
 * This version works directly with sqlpp23 result types instead of converting
 * to strings. Conversions happen only at render time.
 * 
 * Template Parameters:
 *   RowType - The sqlpp23 result row type (auto-deduced from query results)
 * 
 * Features:
 * - Zero locks on render path (atomic swap only)
 * - Works directly with sqlpp23 typed data
 * - Type-safe column extraction
 * - Sorting on typed data (numeric vs string)
 * - Customizable formatters per column
 * 
 * Example:
 *   // Query returns result with typed rows
 *   auto results = db(select(all_of(test_db::foo)).from(test_db::foo));
 *   using RowType = decltype(results.front());
 *   
 *   AsyncTableWidgetTyped<RowType> widget;
 *   widget.AddColumn("ID", [](const RowType& row) { return row.Id; });
 *   widget.AddColumn("Name", [](const RowType& row) { return row.Name; });
 *   widget.SetRefreshCallback([&db](auto& rows) {
 *       rows.clear();
 *       for (const auto& row : db(select(all_of(test_db::foo)).from(test_db::foo))) {
 *           rows.push_back(row);
 *       }
 *   });
 */
template<typename RowType>
class AsyncTableWidgetTyped {
public:
    /**
     * @brief Column value extractor - extracts typed value from row
     * Returns std::any to handle different column types (int64, string, bool, etc.)
     */
    using ColumnExtractor = std::function<std::any(const RowType&)>;
    
    /**
     * @brief Formatter - converts extracted value to display string
     * Input is std::any containing the typed column value
     */
    using ColumnFormatter = std::function<std::string(const std::any&)>;
    
    /**
     * @brief Cell renderer - custom rendering for a cell
     * Return true if handled, false to use default text rendering
     */
    using CellRenderer = std::function<bool(const RowType&, int colIndex)>;
    
    /**
     * @brief Comparator for sorting - compares two rows by column
     */
    using RowComparator = std::function<bool(const RowType&, const RowType&)>;
    
    /**
     * @brief Column configuration
     */
    struct ColumnConfig {
        std::string header;
        float width = 0.0f;
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None;
        
        ColumnExtractor extractor;      // Extract typed value from row
        ColumnFormatter formatter;      // Convert value to string
        CellRenderer cellRenderer;      // Optional custom renderer
        
        // Icon support
        ImTextureID iconTexture = 0;
        ImVec2 iconSize = ImVec2(16, 16);
        
        ColumnConfig(const std::string& h, ColumnExtractor ext) 
            : header(h), extractor(ext) {
            // Default formatter: try to convert std::any to string
            formatter = [](const std::any& val) -> std::string {
                if (!val.has_value()) return "";
                
                // Try common types
                if (auto* v = std::any_cast<int64_t>(&val)) {
                    return std::to_string(*v);
                } else if (auto* v = std::any_cast<int>(&val)) {
                    return std::to_string(*v);
                } else if (auto* v = std::any_cast<double>(&val)) {
                    return std::to_string(*v);
                } else if (auto* v = std::any_cast<bool>(&val)) {
                    return *v ? "true" : "false";
                } else if (auto* v = std::any_cast<std::string>(&val)) {
                    return *v;
                } else if (auto* v = std::any_cast<const char*>(&val)) {
                    return std::string(*v);
                } else if (auto* v = std::any_cast<std::string_view>(&val)) {
                    return std::string(*v);
                }
                
                return "[unknown type]";
            };
        }
    };

private:
    // Double buffer for rows (typed sqlpp23 result rows)
    std::vector<RowType> m_buffers[2];
    std::atomic<int> m_frontIndex{0};
    
    // Column definitions
    std::vector<ColumnConfig> m_columns;
    
    // Refresh callback (called on background thread)
    std::function<void(std::vector<RowType>&)> m_refreshCallback;
    
    // ImGui table state
    std::string m_tableId;
    ImGuiTableFlags m_tableFlags = ImGuiTableFlags_Borders | 
                                   ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_Sortable |
                                   ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_Reorderable;
    
    // Filter/search
    char m_filterBuffer[256] = "";
    bool m_filterEnabled = false;
    
    // Sorting state
    int m_lastSortColumn = -1;
    ImGuiSortDirection m_lastSortDirection = ImGuiSortDirection_None;

public:
    AsyncTableWidgetTyped() {
        static int s_tableCounter = 0;
        m_tableId = "AsyncTableTyped##" + std::to_string(s_tableCounter++);
    }
    
    /**
     * @brief Add a column with typed extractor
     * 
     * Example:
     *   widget.AddColumn("ID", [](const auto& row) { return row.Id; });
     *   widget.AddColumn("Name", [](const auto& row) { return std::string(row.Name); });
     *   widget.AddColumn("Active", [](const auto& row) { return row.HasFun; });
     */
    void AddColumn(
        const std::string& header,
        ColumnExtractor extractor,
        float width = 0.0f,
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None)
    {
        ColumnConfig col(header, extractor);
        col.width = width;
        col.flags = flags;
        m_columns.push_back(col);
    }
    
    /**
     * @brief Set custom formatter for a column
     * 
     * Example:
     *   widget.SetColumnFormatter(1, [](const std::any& val) {
     *       auto name = std::any_cast<std::string>(val);
     *       return "User: " + name;
     *   });
     */
    void SetColumnFormatter(size_t colIndex, ColumnFormatter formatter) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].formatter = formatter;
        }
    }
    
    /**
     * @brief Set enum formatter (maps value to display text)
     * 
     * Example:
     *   widget.SetColumnEnumFormatter(2, [](const std::any& val) {
     *       bool active = std::any_cast<bool>(val);
     *       return active ? "✓ Active" : "✗ Inactive";
     *   });
     */
    void SetColumnEnumFormatter(size_t colIndex, ColumnFormatter formatter) {
        SetColumnFormatter(colIndex, formatter);
    }
    
    /**
     * @brief Set custom cell renderer
     */
    void SetColumnCellRenderer(size_t colIndex, CellRenderer renderer) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].cellRenderer = renderer;
        }
    }
    
    /**
     * @brief Set icon for column
     */
    void SetColumnIcon(size_t colIndex, ImTextureID texture, ImVec2 size = ImVec2(16, 16)) {
        if (colIndex < m_columns.size()) {
            m_columns[colIndex].iconTexture = texture;
            m_columns[colIndex].iconSize = size;
        }
    }
    
    /**
     * @brief Set refresh callback - populates buffer with typed rows
     */
    void SetRefreshCallback(std::function<void(std::vector<RowType>&)> callback) {
        m_refreshCallback = callback;
    }
    
    /**
     * @brief Enable filter bar
     */
    void EnableFilter(bool enable = true) {
        m_filterEnabled = enable;
    }
    
    /**
     * @brief Render the table (call every frame from GUI thread)
     * LOCK-FREE - only reads front buffer via atomic load
     */
    void Render() {
        // Atomic read (acquire semantics)
        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        const std::vector<RowType>& rows = m_buffers[frontIdx];
        
        // Optional filter bar
        if (m_filterEnabled) {
            ImGui::InputText("Filter", m_filterBuffer, sizeof(m_filterBuffer));
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                m_filterBuffer[0] = '\0';
            }
        }
        
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
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    m_lastSortColumn = spec.ColumnIndex;
                    m_lastSortDirection = spec.SortDirection;
                    sortSpecs->SpecsDirty = false;
                }
            }
            
            // Use ImGuiListClipper for efficient rendering
            ImGuiListClipper clipper;
            clipper.Begin(rows.size());
            
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                    const RowType& rowData = rows[row];
                    
                    // Apply filter (extract and check all columns)
                    if (m_filterEnabled && m_filterBuffer[0] != '\0') {
                        bool matches = false;
                        for (size_t col = 0; col < m_columns.size(); col++) {
                            auto value = m_columns[col].extractor(rowData);
                            std::string text = m_columns[col].formatter(value);
                            if (text.find(m_filterBuffer) != std::string::npos) {
                                matches = true;
                                break;
                            }
                        }
                        if (!matches) continue;
                    }
                    
                    ImGui::TableNextRow();
                    
                    // Render cells
                    for (size_t col = 0; col < m_columns.size(); col++) {
                        ImGui::TableSetColumnIndex(col);
                        
                        const auto& colCfg = m_columns[col];
                        bool rendered = false;
                        
                        // Try custom cell renderer first
                        if (colCfg.cellRenderer) {
                            rendered = colCfg.cellRenderer(rowData, col);
                        }
                        
                        // If not custom rendered, show icon if available
                        if (!rendered && colCfg.iconTexture) {
                            ImGui::Image(colCfg.iconTexture, colCfg.iconSize);
                            ImGui::SameLine();
                        }
                        
                        // Render text (unless custom renderer handled it)
                        if (!rendered) {
                            // Extract typed value and format to string
                            auto value = colCfg.extractor(rowData);
                            std::string text = colCfg.formatter(value);
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
     * Sorting is applied here (not in Render) to maintain zero-lock rendering.
     * Sorts on typed data for accurate numeric/string comparisons.
     */
    void Refresh() {
        if (!m_refreshCallback) {
            return;
        }
        
        // Determine back buffer index
        int currentFront = m_frontIndex.load(std::memory_order_relaxed);
        int backIdx = 1 - currentFront;
        
        // Clear and populate back buffer
        std::vector<RowType>& backBuffer = m_buffers[backIdx];
        backBuffer.clear();
        
        // Execute refresh callback
        m_refreshCallback(backBuffer);
        
        // Apply sorting if requested (sort on TYPED data!)
        if (m_lastSortColumn >= 0 && m_lastSortColumn < (int)m_columns.size()) {
            bool ascending = (m_lastSortDirection == ImGuiSortDirection_Ascending);
            const auto& colCfg = m_columns[m_lastSortColumn];
            
            std::sort(backBuffer.begin(), backBuffer.end(),
                [&colCfg, ascending](const RowType& a, const RowType& b) {
                    auto aVal = colCfg.extractor(a);
                    auto bVal = colCfg.extractor(b);
                    
                    // Compare by type
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
                    
                    // Fallback: compare as strings
                    std::string aStr = colCfg.formatter(aVal);
                    std::string bStr = colCfg.formatter(bVal);
                    return ascending ? (aStr < bStr) : (aStr > bStr);
                });
        }
        
        // Atomic swap (release semantics ensures all writes visible)
        m_frontIndex.store(backIdx, std::memory_order_release);
    }
    
    /**
     * @brief Manually set data (for testing or one-time loads)
     */
    void SetData(std::vector<RowType>&& rows) {
        int currentFront = m_frontIndex.load(std::memory_order_relaxed);
        int backIdx = 1 - currentFront;
        m_buffers[backIdx] = std::move(rows);
        m_frontIndex.store(backIdx, std::memory_order_release);
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
    
    // Feature control methods
    void SetTableFlags(ImGuiTableFlags flags) { m_tableFlags = flags; }
    ImGuiTableFlags GetTableFlags() const { return m_tableFlags; }
    void EnableSorting(bool enable = true) {
        if (enable) m_tableFlags |= ImGuiTableFlags_Sortable;
        else m_tableFlags &= ~ImGuiTableFlags_Sortable;
    }
    void EnableResizing(bool enable = true) {
        if (enable) m_tableFlags |= ImGuiTableFlags_Resizable;
        else m_tableFlags &= ~ImGuiTableFlags_Resizable;
    }
    void EnableReordering(bool enable = true) {
        if (enable) m_tableFlags |= ImGuiTableFlags_Reorderable;
        else m_tableFlags &= ~ImGuiTableFlags_Reorderable;
    }
    
    int GetSortColumn() const { return m_lastSortColumn; }
    ImGuiSortDirection GetSortDirection() const { return m_lastSortDirection; }
    void SetSort(int column, ImGuiSortDirection direction) {
        m_lastSortColumn = column;
        m_lastSortDirection = direction;
    }
};

} // namespace db
