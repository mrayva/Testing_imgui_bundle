#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <algorithm>
#include <set>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <type_traits>
#include "imgui.h"

namespace db {

/**
 * @brief C++20 concept for collections compatible with ReactiveListWidget
 *
 * Any collection exposing these types and methods can be used:
 *   - elem1_type, elem2_type, total1_type, total2_type, id_type
 *   - size(), total1(), total2(), begin(), end()
 */
template <typename C>
concept ReactiveCollection = requires(const C& c) {
    typename C::elem1_type;
    typename C::elem2_type;
    typename C::total1_type;
    typename C::total2_type;
    typename C::id_type;
    { c.size() } -> std::convertible_to<std::size_t>;
    { c.total1() } -> std::convertible_to<typename C::total1_type>;
    { c.total2() } -> std::convertible_to<typename C::total2_type>;
    { c.begin() };
    { c.end() };
};

/**
 * @brief Reactive list widget for ImGui — typed, double-buffered, zero-lock rendering
 *
 * Displays a ReactiveTwoFieldCollection as a 3-column table (ID, Elem1, Elem2)
 * with a totals footer row. Uses compile-time typed columns instead of type-erased
 * std::any, paired with double-buffered snapshots for lock-free rendering.
 *
 * @tparam CollectionT A type satisfying the ReactiveCollection concept
 *
 * Features:
 * - Zero locks on render path (atomic buffer swap)
 * - Compile-time typed sorting (no std::any)
 * - Multi-column sort with tristate (Shift+click)
 * - ImGuiListClipper for virtualized large lists
 * - Frozen header row
 * - Row selection (ImGui MultiSelect API)
 * - Per-row and per-cell background color callbacks
 * - Right-click context menu callback
 * - Filter/search bar
 * - Scroll-to-row support
 * - Copy selection to clipboard
 * - Totals footer row (always visible, styled)
 * - Customizable column headers, widths, and string formatters
 */
template <typename CollectionT>
    requires ReactiveCollection<CollectionT>
class ReactiveListWidget {
public:
    using id_type    = typename CollectionT::id_type;
    using elem1_type = typename CollectionT::elem1_type;
    using elem2_type = typename CollectionT::elem2_type;
    using total1_type = typename CollectionT::total1_type;
    using total2_type = typename CollectionT::total2_type;

    struct SnapshotRow {
        id_type    id;
        elem1_type elem1;
        elem2_type elem2;
        std::string idStr;
        std::string elem1Str;
        std::string elem2Str;
    };

    struct SnapshotTotals {
        total1_type total1{};
        total2_type total2{};
        std::string total1Str;
        std::string total2Str;
        std::size_t count = 0;
    };

    using RowColorCallback  = std::function<ImU32(const SnapshotRow& row, int rowIndex)>;
    using CellColorCallback = std::function<ImU32(const SnapshotRow& row, int rowIndex, int colIndex)>;
    using ContextMenuCallback = std::function<void(const SnapshotRow& row, int rowIndex)>;

    struct SortSpec {
        int columnIndex = -1;
        int direction   = ImGuiSortDirection_None;
    };

    ReactiveListWidget() {
        static int s_counter = 0;
        m_tableId = "ReactiveList##" + std::to_string(s_counter++);
    }

    // ---- Column configuration ----

    void SetColumnHeaders(const std::string& idHeader, const std::string& elem1Header,
                          const std::string& elem2Header) {
        m_headers[0] = idHeader;
        m_headers[1] = elem1Header;
        m_headers[2] = elem2Header;
    }

    void SetColumnWidths(float idWidth, float elem1Width, float elem2Width) {
        m_widths[0] = idWidth;
        m_widths[1] = elem1Width;
        m_widths[2] = elem2Width;
    }

    // ---- String formatters ----

    void SetIdFormatter(std::function<std::string(id_type)> fn) { m_idFmt = std::move(fn); }
    void SetElem1Formatter(std::function<std::string(elem1_type)> fn) { m_elem1Fmt = std::move(fn); }
    void SetElem2Formatter(std::function<std::string(elem2_type)> fn) { m_elem2Fmt = std::move(fn); }
    void SetTotal1Formatter(std::function<std::string(total1_type)> fn) { m_total1Fmt = std::move(fn); }
    void SetTotal2Formatter(std::function<std::string(total2_type)> fn) { m_total2Fmt = std::move(fn); }

    // ---- Feature toggles ----

    void EnableFilter(bool enable = true)    { m_filterEnabled = enable; }
    void EnableSelection(bool enable = true) { m_selectionEnabled = enable; }
    void SetTableFlags(ImGuiTableFlags flags) { m_tableFlags = flags; }

    void SetScrollFreeze(int cols, int rows) {
        m_frozenColumns = cols;
        m_frozenRows = rows;
    }

    // ---- Callbacks ----

    void SetRowColorCallback(RowColorCallback cb)       { m_rowColorCb = std::move(cb); }
    void SetCellColorCallback(CellColorCallback cb)     { m_cellColorCb = std::move(cb); }
    void SetContextMenuCallback(ContextMenuCallback cb) { m_contextMenuCb = std::move(cb); }

    // ---- Scroll / selection ----

    void ScrollToRow(int rowIndex) { m_scrollToRow = rowIndex; }
    const ImGuiSelectionBasicStorage& GetSelection() const { return m_selection; }
    ImGuiSelectionBasicStorage& GetSelection() { return m_selection; }
    void ClearSelection() { m_selection.Clear(); }

    std::vector<int> GetSelectedIndices() const {
        std::vector<int> result;
        void* it = nullptr;
        ImGuiID id;
        auto& sel = const_cast<ImGuiSelectionBasicStorage&>(m_selection);
        while (sel.GetNextSelectedItem(&it, &id)) {
            result.push_back(static_cast<int>(id));
        }
        return result;
    }

    // ---- Data flow ----

    /**
     * @brief Snapshot the collection into the back buffer, then swap.
     * Call from a background thread or before Render().
     */
    void Refresh(const CollectionT& collection) {
        int currentFront = m_frontIndex.load(std::memory_order_relaxed);
        int backIdx = 1 - currentFront;

        auto& backRows   = m_rowBuffers[backIdx];
        auto& backTotals = m_totalsBuffers[backIdx];

        backRows.clear();

        // Iterate collection — phmap parallel_node_hash_map iterators are safe during concurrent inserts
        for (auto it = collection.begin(); it != collection.end(); ++it) {
            SnapshotRow row;
            row.id    = it->first;
            row.elem1 = it->second.lastElem1;
            row.elem2 = it->second.lastElem2;
            row.idStr    = FormatId(row.id);
            row.elem1Str = FormatElem1(row.elem1);
            row.elem2Str = FormatElem2(row.elem2);
            backRows.push_back(std::move(row));
        }

        // Snapshot totals
        backTotals.total1    = collection.total1();
        backTotals.total2    = collection.total2();
        backTotals.total1Str = FormatTotal1(backTotals.total1);
        backTotals.total2Str = FormatTotal2(backTotals.total2);
        backTotals.count     = collection.size();

        // Apply sorting
        ApplySort(backRows);

        // Atomic swap
        m_frontIndex.store(backIdx, std::memory_order_release);
    }

    /**
     * @brief Render the table. Call every frame from the GUI thread.
     * Lock-free — reads only the front buffer.
     */
    void Render() {
        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        const auto& rows   = m_rowBuffers[frontIdx];
        const auto& totals = m_totalsBuffers[frontIdx];

        // Filter bar
        if (m_filterEnabled) {
            ImGui::InputText("Filter", m_filterBuf, sizeof(m_filterBuf));
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                m_filterBuf[0] = '\0';
            }
        }

        // Build filtered indices
        std::vector<int> filtered;
        if (m_filterEnabled && m_filterBuf[0] != '\0') {
            for (int i = 0; i < (int)rows.size(); i++) {
                const auto& r = rows[i];
                if (r.idStr.find(m_filterBuf) != std::string::npos ||
                    r.elem1Str.find(m_filterBuf) != std::string::npos ||
                    r.elem2Str.find(m_filterBuf) != std::string::npos) {
                    filtered.push_back(i);
                }
            }
        } else {
            filtered.reserve(rows.size());
            for (int i = 0; i < (int)rows.size(); i++) {
                filtered.push_back(i);
            }
        }

        // Row count display
        if (m_selectionEnabled && m_selection.Size > 0) {
            ImGui::Text("%zu rows (%d selected)", filtered.size(), m_selection.Size);
        } else {
            ImGui::Text("%zu rows", filtered.size());
        }

        // Begin table
        if (!ImGui::BeginTable(m_tableId.c_str(), 3, m_tableFlags)) return;

        // Setup columns
        for (int c = 0; c < 3; c++) {
            ImGui::TableSetupColumn(m_headers[c].c_str(), ImGuiTableColumnFlags_None, m_widths[c]);
        }
        ImGui::TableSetupScrollFreeze(m_frozenColumns, m_frozenRows);
        ImGui::TableHeadersRow();

        // Capture sort spec changes
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsDirty) {
                int count = std::min((int)specs->SpecsCount, kMaxSortSpecs);
                for (int i = 0; i < count; i++) {
                    m_sortSpecs[i].columnIndex = specs->Specs[i].ColumnIndex;
                    m_sortSpecs[i].direction   = specs->Specs[i].SortDirection;
                }
                m_sortSpecCount.store(count, std::memory_order_relaxed);
                m_sortSpecsDirty.store(true, std::memory_order_release);
                specs->SpecsDirty = false;
            }
        }

        // Begin multi-select
        ImGuiMultiSelectIO* msIO = nullptr;
        if (m_selectionEnabled) {
            ImGuiMultiSelectFlags msFlags = ImGuiMultiSelectFlags_ClearOnEscape |
                                            ImGuiMultiSelectFlags_ClearOnClickVoid |
                                            ImGuiMultiSelectFlags_BoxSelect1d;
            msIO = ImGui::BeginMultiSelect(msFlags, m_selection.Size, (int)filtered.size());
            m_selection.ApplyRequests(msIO);
        }

        // Clipper for virtualized rows
        ImGuiListClipper clipper;
        clipper.Begin((int)filtered.size());

        if (m_scrollToRow >= 0 && m_scrollToRow < (int)filtered.size()) {
            clipper.IncludeItemsByIndex(m_scrollToRow, m_scrollToRow + 1);
            ImGui::SetScrollY(m_scrollToRow * ImGui::GetTextLineHeightWithSpacing());
            m_scrollToRow = -1;
        }

        while (clipper.Step()) {
            for (int idx = clipper.DisplayStart; idx < clipper.DisplayEnd; idx++) {
                int dataIdx = filtered[idx];
                const SnapshotRow& row = rows[dataIdx];

                ImGui::TableNextRow();

                // Per-row color
                if (m_rowColorCb) {
                    ImU32 color = m_rowColorCb(row, dataIdx);
                    if (color != 0) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, color);
                    }
                }

                // Column 0: ID
                ImGui::TableSetColumnIndex(0);
                if (m_cellColorCb) {
                    ImU32 cc = m_cellColorCb(row, dataIdx, 0);
                    if (cc != 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cc);
                }
                if (m_selectionEnabled) {
                    ImGui::SetNextItemSelectionUserData(idx);
                    bool selected = m_selection.Contains((ImGuiID)idx);
                    char label[64];
                    snprintf(label, sizeof(label), "##row%d", idx);
                    ImGui::Selectable(label, selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                    if (m_contextMenuCb) {
                        char popupId[64];
                        snprintf(popupId, sizeof(popupId), "ctx##%d", idx);
                        if (ImGui::BeginPopupContextItem(popupId)) {
                            m_contextMenuCb(row, dataIdx);
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::SameLine(0.0f, 0.0f);
                }
                ImGui::TextUnformatted(row.idStr.c_str());

                // Column 1: Elem1
                ImGui::TableSetColumnIndex(1);
                if (m_cellColorCb) {
                    ImU32 cc = m_cellColorCb(row, dataIdx, 1);
                    if (cc != 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cc);
                }
                ImGui::TextUnformatted(row.elem1Str.c_str());

                // Column 2: Elem2
                ImGui::TableSetColumnIndex(2);
                if (m_cellColorCb) {
                    ImU32 cc = m_cellColorCb(row, dataIdx, 2);
                    if (cc != 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, cc);
                }
                ImGui::TextUnformatted(row.elem2Str.c_str());

                // Context menu for non-selection mode
                if (!m_selectionEnabled && m_contextMenuCb) {
                    char popupId[64];
                    snprintf(popupId, sizeof(popupId), "ctx##%d", idx);
                    if (ImGui::BeginPopupContextItem(popupId)) {
                        m_contextMenuCb(row, dataIdx);
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

        // ---- Totals footer row ----
        ImGui::TableNextRow();
        ImU32 totalsColor = IM_COL32(40, 60, 90, 255);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, totalsColor);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, totalsColor);

        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.6f, 1.0f));
        ImGui::TextUnformatted("TOTALS");

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(totals.total1Str.c_str());

        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(totals.total2Str.c_str());
        ImGui::PopStyleColor();

        ImGui::EndTable();
    }

    // ---- Utility ----

    void CopySelectionToClipboard() const {
        if (!m_selectionEnabled || m_selection.Size == 0) return;

        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        const auto& rows = m_rowBuffers[frontIdx];

        std::string text;
        text += m_headers[0] + '\t' + m_headers[1] + '\t' + m_headers[2] + '\n';

        void* it = nullptr;
        ImGuiID id;
        auto& sel = const_cast<ImGuiSelectionBasicStorage&>(m_selection);
        while (sel.GetNextSelectedItem(&it, &id)) {
            int idx = static_cast<int>(id);
            if (idx >= 0 && idx < (int)rows.size()) {
                text += rows[idx].idStr + '\t' + rows[idx].elem1Str + '\t' + rows[idx].elem2Str + '\n';
            }
        }

        ImGui::SetClipboardText(text.c_str());
    }

    size_t GetRowCount() const {
        int frontIdx = m_frontIndex.load(std::memory_order_acquire);
        return m_rowBuffers[frontIdx].size();
    }

private:
    // ---- Formatting helpers ----

    std::string FormatId(id_type v) const {
        if (m_idFmt) return m_idFmt(v);
        return DefaultFormat(v);
    }
    std::string FormatElem1(elem1_type v) const {
        if (m_elem1Fmt) return m_elem1Fmt(v);
        return DefaultFormat(v);
    }
    std::string FormatElem2(elem2_type v) const {
        if (m_elem2Fmt) return m_elem2Fmt(v);
        return DefaultFormat(v);
    }
    std::string FormatTotal1(total1_type v) const {
        if (m_total1Fmt) return m_total1Fmt(v);
        return DefaultFormat(v);
    }
    std::string FormatTotal2(total2_type v) const {
        if (m_total2Fmt) return m_total2Fmt(v);
        return DefaultFormat(v);
    }

    template <typename T>
    static std::string DefaultFormat(T v) {
        if constexpr (std::is_floating_point_v<T>) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << v;
            return oss.str();
        } else if constexpr (std::is_integral_v<T>) {
            return std::to_string(v);
        } else {
            std::ostringstream oss;
            oss << v;
            return oss.str();
        }
    }

    // ---- Sorting ----

    void ApplySort(std::vector<SnapshotRow>& rows) {
        int specCount = m_sortSpecCount.load(std::memory_order_acquire);
        if (specCount <= 0) return;

        SortSpec specs[kMaxSortSpecs];
        for (int i = 0; i < specCount; i++) {
            specs[i] = m_sortSpecs[i];
        }

        std::stable_sort(rows.begin(), rows.end(),
            [&specs, specCount](const SnapshotRow& a, const SnapshotRow& b) {
                for (int s = 0; s < specCount; s++) {
                    int col = specs[s].columnIndex;
                    bool asc = (specs[s].direction == ImGuiSortDirection_Ascending);
                    int cmp = 0;
                    switch (col) {
                        case 0: cmp = CompareValues(a.id, b.id); break;
                        case 1: cmp = CompareValues(a.elem1, b.elem1); break;
                        case 2: cmp = CompareValues(a.elem2, b.elem2); break;
                        default: break;
                    }
                    if (cmp != 0) return asc ? (cmp < 0) : (cmp > 0);
                }
                return false;
            });

        m_sortSpecsDirty.store(false, std::memory_order_relaxed);
    }

    template <typename T>
    static int CompareValues(const T& a, const T& b) {
        if constexpr (requires { a < b; }) {
            if (a < b) return -1;
            if (b < a) return 1;
            return 0;
        } else {
            return 0;
        }
    }

    // ---- Members ----

    // Double buffers
    std::vector<SnapshotRow> m_rowBuffers[2];
    SnapshotTotals           m_totalsBuffers[2];
    std::atomic<int>         m_frontIndex{0};

    // Table config
    std::string    m_tableId;
    std::string    m_headers[3] = {"ID", "Elem1", "Elem2"};
    float          m_widths[3]  = {80.0f, 150.0f, 150.0f};
    ImGuiTableFlags m_tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
                                   ImGuiTableFlags_SortMulti | ImGuiTableFlags_SortTristate;

    // Filter
    char m_filterBuf[256] = "";
    bool m_filterEnabled  = false;

    // Sort
    static constexpr int kMaxSortSpecs = 4;
    std::atomic<int>  m_sortSpecCount{0};
    SortSpec          m_sortSpecs[kMaxSortSpecs];
    std::atomic<bool> m_sortSpecsDirty{false};

    // Freeze
    int m_frozenColumns = 0;
    int m_frozenRows    = 1;

    // Selection
    bool m_selectionEnabled = false;
    ImGuiSelectionBasicStorage m_selection;

    // Callbacks
    RowColorCallback    m_rowColorCb;
    CellColorCallback   m_cellColorCb;
    ContextMenuCallback m_contextMenuCb;

    // Scroll
    int m_scrollToRow = -1;

    // Formatters
    std::function<std::string(id_type)>    m_idFmt;
    std::function<std::string(elem1_type)> m_elem1Fmt;
    std::function<std::string(elem2_type)> m_elem2Fmt;
    std::function<std::string(total1_type)> m_total1Fmt;
    std::function<std::string(total2_type)> m_total2Fmt;
};

} // namespace db
