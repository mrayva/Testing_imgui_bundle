#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <any>
#include "imgui.h"
#include "database_manager.h"

namespace db {

/**
 * @brief Async table widget for ImGui with zero-lock rendering
 * 
 * Uses double-buffering to display database query results that can be
 * updated in background threads without blocking the UI.
 * 
 * Features:
 * - Zero locks on render path (atomic swap only)
 * - ImGuiListClipper for efficient large lists
 * - Customizable column formatters
 * - Background refresh support
 * 
 * Example:
 *   AsyncTableWidget widget;
 *   widget.AddColumn("ID");
 *   widget.AddColumn("Name");
 *   widget.SetRefreshCallback([&](auto& rows) {
 *       FooRepository repo(db);
 *       auto results = repo.SelectAll();
 *       for (auto& r : results) {
 *           rows.push_back({std::to_string(r.id), r.name, r.has_fun ? "Yes" : "No"});
 *       }
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
     */
    struct Row {
        // Raw column data as strings (easy to display, compare, filter)
        std::vector<std::string> columns;
        
        // Optional: Store original row for custom formatters
        // This allows formatters to access typed data
        std::any userData;
        
        Row() = default;
        
        explicit Row(size_t numColumns) : columns(numColumns) {}
        
        Row(std::initializer_list<std::string> cols) : columns(cols) {}
    };
    
    /**
     * @brief Column configuration
     */
    struct ColumnConfig {
        std::string header;
        float width = 0.0f;  // 0 = auto-size
        ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None;
        
        // Optional: Custom formatter (accesses Row.userData if needed)
        std::function<std::string(const Row&, int colIndex)> formatter;
        
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
                                   ImGuiTableFlags_ScrollY;
    
    // Optional: Filter/search
    char m_filterBuffer[256] = "";
    bool m_filterEnabled = false;

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
     * @param flags ImGui column flags
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
                        
                        // Use custom formatter if provided
                        if (m_columns[col].formatter) {
                            std::string formatted = m_columns[col].formatter(rowData, col);
                            ImGui::TextUnformatted(formatted.c_str());
                        } else {
                            ImGui::TextUnformatted(rowData.columns[col].c_str());
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
};

} // namespace db
