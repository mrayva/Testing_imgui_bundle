#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <memory>
#include <any>
#include <algorithm>
#include <set>
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
 * - Multi-column sort (Shift+click headers)
 * - ImGuiListClipper for efficient large lists
 * - Frozen header row / frozen left columns
 * - Row selection (single or multi via ImGui MultiSelect API)
 * - Per-row and per-cell background colors
 * - Right-click context menu callback
 * - Scroll-to-row support
 * - Column hide/show, stretch modes, horizontal scroll
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
     * Supports type erasure: Store the original typed sqlpp23 row
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

        // Constructor that stores both strings and typed data
        Row(std::initializer_list<std::string> cols, std::any typedData) : columns(cols), userData(typedData) {}
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
     * @brief Row color callback - returns background color for a row
     * Return 0 (transparent) to use default alternating colors.
     */
    using RowColorCallback = std::function<ImU32(const Row& row, int rowIndex)>;

    /**
     * @brief Cell color callback - returns background color for a specific cell
     * Return 0 (transparent) to use default.
     */
    using CellColorCallback = std::function<ImU32(const Row& row, int rowIndex, int colIndex)>;

    /**
     * @brief Context menu callback - called when user right-clicks a row
     * Should call ImGui::MenuItem() etc. inside.
     */
    using ContextMenuCallback = std::function<void(const Row& row, int rowIndex)>;

    /**
     * @brief Column configuration with advanced features
     */
    struct ColumnConfig {
        std::string header;
        float width = 0.0f; // 0 = auto-size
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
        ImTextureID iconTexture = 0; // nullptr is not valid for ImTextureID (it's unsigned long long)
        ImVec2 iconSize = ImVec2(16, 16);

        // Optional: Enum converter (maps string value to display text)
        std::function<std::string(const std::string&)> enumFormatter;

        ColumnConfig(const std::string& h, float w = 0.0f) : header(h), width(w) {}
    };

    /**
     * @brief Sort spec for multi-column sorting (shared between threads)
     */
    struct SortSpec {
        int columnIndex = -1;
        int direction = ImGuiSortDirection_None; // ImGuiSortDirection_Ascending or _Descending
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
    ImGuiTableFlags m_tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                   ImGuiTableFlags_Hideable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_SortTristate |
                                   ImGuiTableFlags_SizingFixedFit;

    // Optional: Filter/search
    char m_filterBuffer[256] = "";
    bool m_filterEnabled = false;

    // Multi-column sort state (shared between GUI and background thread)
    static constexpr int kMaxSortSpecs = 4;
    std::atomic<int> m_sortSpecCount{0};
    SortSpec m_sortSpecs[kMaxSortSpecs]; // Written by GUI thread, read by background thread
    std::atomic<bool> m_sortSpecsDirty{false}; // Signal that specs changed

    // Frozen rows/columns
    int m_frozenColumns = 0;
    int m_frozenRows = 1; // 1 = freeze header row (default)

    // Selection state
    bool m_selectionEnabled = false;
    ImGuiSelectionBasicStorage m_selection;

    // Color callbacks
    RowColorCallback m_rowColorCallback;
    CellColorCallback m_cellColorCallback;

    // Context menu
    ContextMenuCallback m_contextMenuCallback;

    // Scroll-to-row request (-1 = none)
    int m_scrollToRow = -1;

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
     * @param width Column width (0 = auto). For stretch columns, this is a weight.
     * @param flags ImGui column flags (includes sorting, resizing options)
     * @param formatter Optional custom formatter function
     */
    void AddColumn(const std::string& header, float width = 0.0f,
                   ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_None,
                   std::function<std::string(const Row&, int)> formatter = nullptr) {
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
    void SetRefreshCallback(std::function<void(std::vector<Row>&)> callback) { m_refreshCallback = callback; }

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
    void SetTableFlags(ImGuiTableFlags flags) { m_tableFlags = flags; }

    /**
     * @brief Enable search/filter bar
     */
    void EnableFilter(bool enable = true) { m_filterEnabled = enable; }

    /**
     * @brief Enable row selection (single or multi)
     */
    void EnableSelection(bool enable = true) { m_selectionEnabled = enable; }

    /**
     * @brief Set frozen columns/rows for scroll freeze
     * @param cols Number of leftmost columns to freeze (0 = none)
     * @param rows Number of topmost rows to freeze (1 = header only, default)
     */
    void SetScrollFreeze(int cols, int rows) {
        m_frozenColumns = cols;
        m_frozenRows = rows;
    }

    /**
     * @brief Set callback for per-row background color
     * Return 0 for default color, or an ImU32 color value.
     */
    void SetRowColorCallback(RowColorCallback callback) { m_rowColorCallback = callback; }

    /**
     * @brief Set callback for per-cell background color
     * Return 0 for default color, or an ImU32 color value.
     */
    void SetCellColorCallback(CellColorCallback callback) { m_cellColorCallback = callback; }

    /**
     * @brief Set right-click context menu callback for rows
     */
    void SetContextMenuCallback(ContextMenuCallback callback) { m_contextMenuCallback = callback; }

    /**
     * @brief Scroll to a specific row index on next Render()
     */
    void ScrollToRow(int rowIndex) { m_scrollToRow = rowIndex; }

    /**
     * @brief Get selection state (read-only access)
     */
    const ImGuiSelectionBasicStorage& GetSelection() const { return m_selection; }

    /**
     * @brief Get mutable selection state
     */
    ImGuiSelectionBasicStorage& GetSelection() { return m_selection; }

    /**
     * @brief Clear selection
     */
    void ClearSelection() { m_selection.Clear(); }

    /**
     * @brief Get selected row indices from current front buffer
     */
    std::vector<int> GetSelectedIndices() const {
        std::vector<int> result;
        void* it = nullptr;
        ImGuiID id;
        // const_cast needed because ImGui's GetNextSelectedItem isn't const
        auto& sel = const_cast<ImGuiSelectionBasicStorage&>(m_selection);
        while (sel.GetNextSelectedItem(&it, &id)) {
            result.push_back(static_cast<int>(id));
        }
        return result;
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

        // Build filtered row indices (must happen before clipper)
        std::vector<int> filteredIndices;
        if (m_filterEnabled && m_filterBuffer[0] != '\0') {
            for (int i = 0; i < (int)rows.size(); i++) {
                for (const auto& col : rows[i].columns) {
                    if (col.find(m_filterBuffer) != std::string::npos) {
                        filteredIndices.push_back(i);
                        break;
                    }
                }
            }
        } else {
            filteredIndices.reserve(rows.size());
            for (int i = 0; i < (int)rows.size(); i++) {
                filteredIndices.push_back(i);
            }
        }

        // Show row count (and selection count if enabled)
        if (m_selectionEnabled && m_selection.Size > 0) {
            ImGui::Text("%zu rows (%d selected)", filteredIndices.size(), m_selection.Size);
        } else {
            ImGui::Text("%zu rows", filteredIndices.size());
        }

        // Render table
        if (ImGui::BeginTable(m_tableId.c_str(), m_columns.size(), m_tableFlags)) {
            // Setup columns
            for (const auto& col : m_columns) {
                ImGui::TableSetupColumn(col.header.c_str(), col.flags, col.width);
            }

            // Freeze header row and optionally left columns
            ImGui::TableSetupScrollFreeze(m_frozenColumns, m_frozenRows);

            ImGui::TableHeadersRow();

            // Check for sorting changes (supports multi-column sort)
            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                if (sortSpecs->SpecsDirty) {
                    int count = std::min((int)sortSpecs->SpecsCount, kMaxSortSpecs);
                    for (int i = 0; i < count; i++) {
                        m_sortSpecs[i].columnIndex = sortSpecs->Specs[i].ColumnIndex;
                        m_sortSpecs[i].direction = sortSpecs->Specs[i].SortDirection;
                    }
                    m_sortSpecCount.store(count, std::memory_order_relaxed);
                    m_sortSpecsDirty.store(true, std::memory_order_release);

                    sortSpecs->SpecsDirty = false;
                }
            }

            // Begin multi-select if enabled
            ImGuiMultiSelectIO* msIO = nullptr;
            if (m_selectionEnabled) {
                ImGuiMultiSelectFlags msFlags = ImGuiMultiSelectFlags_ClearOnEscape |
                                                ImGuiMultiSelectFlags_ClearOnClickVoid |
                                                ImGuiMultiSelectFlags_BoxSelect1d;
                msIO = ImGui::BeginMultiSelect(msFlags, m_selection.Size, (int)filteredIndices.size());
                m_selection.ApplyRequests(msIO);
            }

            // Use ImGuiListClipper with correct filtered count
            ImGuiListClipper clipper;
            clipper.Begin(filteredIndices.size());

            // Handle scroll-to-row request
            if (m_scrollToRow >= 0 && m_scrollToRow < (int)filteredIndices.size()) {
                clipper.IncludeItemsByIndex(m_scrollToRow, m_scrollToRow + 1);
                ImGui::SetScrollY(m_scrollToRow * ImGui::GetTextLineHeightWithSpacing());
                m_scrollToRow = -1;
            }

            while (clipper.Step()) {
                for (int idx = clipper.DisplayStart; idx < clipper.DisplayEnd; idx++) {
                    int dataIdx = filteredIndices[idx];
                    const Row& rowData = rows[dataIdx];

                    ImGui::TableNextRow();

                    // Per-row background color
                    if (m_rowColorCallback) {
                        ImU32 rowColor = m_rowColorCallback(rowData, dataIdx);
                        if (rowColor != 0) {
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, rowColor);
                        }
                    }

                    // Render cells
                    for (size_t col = 0; col < m_columns.size() && col < rowData.columns.size(); col++) {
                        ImGui::TableSetColumnIndex(col);

                        // Per-cell background color
                        if (m_cellColorCallback) {
                            ImU32 cellColor = m_cellColorCallback(rowData, dataIdx, col);
                            if (cellColor != 0) {
                                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cellColor);
                            }
                        }

                        // Selection: render selectable in first column
                        if (m_selectionEnabled && col == 0) {
                            ImGui::SetNextItemSelectionUserData(idx);
                            bool isSelected = m_selection.Contains((ImGuiID)idx);

                            // Selectable spanning all columns for row selection
                            char label[64];
                            snprintf(label, sizeof(label), "##row%d", idx);
                            ImGui::Selectable(label, isSelected,
                                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);

                            // Context menu on right-click
                            if (m_contextMenuCallback) {
                                char popupId[64];
                                snprintf(popupId, sizeof(popupId), "ctx##%d", idx);
                                if (ImGui::BeginPopupContextItem(popupId)) {
                                    m_contextMenuCallback(rowData, dataIdx);
                                    ImGui::EndPopup();
                                }
                            }

                            ImGui::SameLine(0.0f, 0.0f);
                        }

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

                    // Context menu for non-selection mode
                    if (!m_selectionEnabled && m_contextMenuCallback) {
                        char popupId[64];
                        snprintf(popupId, sizeof(popupId), "ctx##%d", idx);
                        if (ImGui::BeginPopupContextItem(popupId)) {
                            m_contextMenuCallback(rowData, dataIdx);
                            ImGui::EndPopup();
                        }
                    }
                }
            }

            // End multi-select
            if (m_selectionEnabled && msIO) {
                msIO = ImGui::EndMultiSelect();
                m_selection.ApplyRequests(msIO);
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

        // Apply multi-column sorting if requested
        int specCount = m_sortSpecCount.load(std::memory_order_acquire);
        if (specCount > 0) {
            // Snapshot the sort specs (they may be updated by GUI thread)
            SortSpec specs[kMaxSortSpecs];
            for (int i = 0; i < specCount; i++) {
                specs[i] = m_sortSpecs[i];
            }

            // Validate all sort specs have typed extractors
            bool canSort = true;
            for (int i = 0; i < specCount; i++) {
                int colIdx = specs[i].columnIndex;
                if (colIdx < 0 || colIdx >= (int)m_columns.size() || !m_columns[colIdx].typedExtractor) {
                    if (colIdx >= 0 && colIdx < (int)m_columns.size()) {
                        std::cerr << "ERROR: Column '" << m_columns[colIdx].header
                                  << "' is sortable but has no typedExtractor. Skipping sort.\n";
                    }
                    canSort = false;
                    break;
                }
            }

            if (canSort) {
                std::stable_sort(backBuffer.begin(), backBuffer.end(),
                                 [this, &specs, specCount](const Row& a, const Row& b) {
                    for (int s = 0; s < specCount; s++) {
                        int colIdx = specs[s].columnIndex;
                        bool ascending = (specs[s].direction == ImGuiSortDirection_Ascending);
                        const auto& colCfg = m_columns[colIdx];

                        int cmp = CompareTypedValues(colCfg.typedExtractor(a), colCfg.typedExtractor(b));
                        if (cmp != 0) {
                            return ascending ? (cmp < 0) : (cmp > 0);
                        }
                    }
                    return false; // Equal across all sort specs
                });
            }

            m_sortSpecsDirty.store(false, std::memory_order_relaxed);
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
    ImGuiTableFlags GetTableFlags() const { return m_tableFlags; }

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

    void EnableHorizontalScroll(bool enable = true) {
        if (enable) {
            m_tableFlags |= ImGuiTableFlags_ScrollX;
        } else {
            m_tableFlags &= ~ImGuiTableFlags_ScrollX;
        }
    }

    void EnableColumnHiding(bool enable = true) {
        if (enable) {
            m_tableFlags |= ImGuiTableFlags_Hideable;
        } else {
            m_tableFlags &= ~ImGuiTableFlags_Hideable;
        }
    }

    /**
     * @brief Set column sizing policy
     * @param policy One of: ImGuiTableFlags_SizingFixedFit, SizingFixedSame,
     *               SizingStretchProp, SizingStretchSame
     */
    void SetSizingPolicy(ImGuiTableFlags policy) {
        // Clear existing sizing bits
        m_tableFlags &= ~(ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_SizingFixedSame |
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_SizingStretchSame);
        m_tableFlags |= policy;
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
    void SetColumnEnumFormatter(size_t colIndex, std::function<std::string(const std::string&)> formatter) {
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
     * @brief Get current sort state (primary sort column)
     */
    int GetSortColumn() const {
        int count = m_sortSpecCount.load(std::memory_order_acquire);
        return count > 0 ? m_sortSpecs[0].columnIndex : -1;
    }

    ImGuiSortDirection GetSortDirection() const {
        int count = m_sortSpecCount.load(std::memory_order_acquire);
        return count > 0 ? static_cast<ImGuiSortDirection>(m_sortSpecs[0].direction) : ImGuiSortDirection_None;
    }

    /**
     * @brief Get full multi-column sort state
     */
    int GetSortSpecCount() const { return m_sortSpecCount.load(std::memory_order_acquire); }

    SortSpec GetSortSpec(int index) const {
        if (index >= 0 && index < m_sortSpecCount.load(std::memory_order_acquire)) {
            return m_sortSpecs[index];
        }
        return {};
    }

    /**
     * @brief Programmatically set sort (will apply on next Refresh)
     */
    void SetSort(int column, ImGuiSortDirection direction) {
        m_sortSpecs[0].columnIndex = column;
        m_sortSpecs[0].direction = direction;
        m_sortSpecCount.store(1, std::memory_order_relaxed);
        m_sortSpecsDirty.store(true, std::memory_order_release);
    }

    /**
     * @brief Copy selected rows to clipboard as tab-separated text
     */
    void CopySelectionToClipboard() const {
        if (!m_selectionEnabled || m_selection.Size == 0) return;

        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        const std::vector<Row>& rows = m_buffers[frontIdx];

        std::string text;

        // Header row
        for (size_t c = 0; c < m_columns.size(); c++) {
            if (c > 0) text += '\t';
            text += m_columns[c].header;
        }
        text += '\n';

        // Data rows
        void* it = nullptr;
        ImGuiID id;
        auto& sel = const_cast<ImGuiSelectionBasicStorage&>(m_selection);
        while (sel.GetNextSelectedItem(&it, &id)) {
            int idx = static_cast<int>(id);
            if (idx >= 0 && idx < (int)rows.size()) {
                for (size_t c = 0; c < rows[idx].columns.size(); c++) {
                    if (c > 0) text += '\t';
                    text += rows[idx].columns[c];
                }
                text += '\n';
            }
        }

        ImGui::SetClipboardText(text.c_str());
    }

private:
    /**
     * @brief Compare two std::any values, returns -1, 0, or 1
     */
    static int CompareTypedValues(const std::any& aVal, const std::any& bVal) {
        if (!aVal.has_value() || !bVal.has_value()) {
            return 0;
        }

        // Try common types in order of likelihood
        if (auto* av = std::any_cast<int64_t>(&aVal)) {
            auto bv = std::any_cast<int64_t>(bVal);
            return (*av < bv) ? -1 : (*av > bv) ? 1 : 0;
        }
        if (auto* av = std::any_cast<int>(&aVal)) {
            auto bv = std::any_cast<int>(bVal);
            return (*av < bv) ? -1 : (*av > bv) ? 1 : 0;
        }
        if (auto* av = std::any_cast<double>(&aVal)) {
            auto bv = std::any_cast<double>(bVal);
            return (*av < bv) ? -1 : (*av > bv) ? 1 : 0;
        }
        if (auto* av = std::any_cast<bool>(&aVal)) {
            auto bv = std::any_cast<bool>(bVal);
            return (*av < bv) ? -1 : (*av > bv) ? 1 : 0;
        }
        if (auto* av = std::any_cast<std::string>(&aVal)) {
            auto& bv = std::any_cast<const std::string&>(bVal);
            return av->compare(bv);
        }
        if (auto* av = std::any_cast<std::string_view>(&aVal)) {
            auto bv = std::any_cast<std::string_view>(bVal);
            return av->compare(bv);
        }

        std::cerr << "WARNING: Unknown type in typedExtractor, cannot sort.\n";
        return 0;
    }
};

} // namespace db
