#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <flatbuffers/flexbuffers.h>

#include "imgui.h"

namespace db {

/**
 * Single-producer, single-consumer mailbox for one immutable FlexBuffer frame.
 * A producer publishes bytes; the UI owns and reads the active slot until the
 * next Sync() call. Frames are dropped when all fixed slots are occupied.
 */
template <std::size_t MaxBinarySize>
class LockFreeFlexbufferMailbox {
private:
    enum class SlotState : std::uint8_t { Empty, Writing, Ready, Reading };

    struct Slot {
        std::array<std::uint8_t, MaxBinarySize> data{};
        std::size_t size = 0;
        std::uint64_t sequence = 0;
        std::atomic<SlotState> state{SlotState::Empty};
    };

public:
    static_assert(MaxBinarySize > 0, "FlexBuffer mailbox capacity must be positive");

    bool Publish(const std::uint8_t* data, std::size_t size) {
        if (data == nullptr || size == 0 || size > MaxBinarySize) {
            m_missedCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        Slot* target = nullptr;
        for (auto& slot : m_slots) {
            SlotState expected = SlotState::Empty;
            if (slot.state.compare_exchange_strong(expected, SlotState::Writing,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                target = &slot;
                break;
            }
        }

        if (target == nullptr) {
            m_missedCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::memcpy(target->data.data(), data, size);
        target->size = size;
        target->sequence = ++m_nextSequence;
        target->state.store(SlotState::Ready, std::memory_order_release);
        return true;
    }

    bool Publish(const flexbuffers::Builder& builder) {
        const auto& buffer = builder.GetBuffer();
        return Publish(buffer.data(), buffer.size());
    }

    /** Call once at the beginning of each UI frame. */
    bool Sync() {
        bool changed = false;
        int newest = -1;
        std::uint64_t newestSequence = 0;
        for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
            if (m_slots[i].state.load(std::memory_order_acquire) == SlotState::Ready &&
                (newest < 0 || m_slots[i].sequence > newestSequence)) {
                newest = i;
                newestSequence = m_slots[i].sequence;
            }
        }

        if (newest >= 0) {
            changed = true;
            m_slots[newest].state.store(SlotState::Reading, std::memory_order_relaxed);
            if (m_activeIndex >= 0) {
                m_slots[m_activeIndex].state.store(SlotState::Empty, std::memory_order_release);
            }
            m_activeIndex = newest;

            for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
                if (i != newest) {
                    SlotState expected = SlotState::Ready;
                    if (m_slots[i].state.compare_exchange_strong(expected, SlotState::Empty,
                                                                  std::memory_order_acq_rel,
                                                                  std::memory_order_relaxed)) {
                        m_missedCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }

        m_uiMissedCount = m_missedCount.load(std::memory_order_relaxed);
        return changed;
    }

    flexbuffers::Reference Root() const {
        if (m_activeIndex < 0 || m_slots[m_activeIndex].size == 0) {
            return flexbuffers::Reference();
        }
        const auto& active = m_slots[m_activeIndex];
        return flexbuffers::GetRoot(active.data.data(), active.size);
    }

    std::size_t MissedCount() const { return m_uiMissedCount; }

private:
    std::array<Slot, 3> m_slots{};
    int m_activeIndex = -1;
    std::uint64_t m_nextSequence = 0;
    std::atomic<std::size_t> m_missedCount{0};
    std::size_t m_uiMissedCount = 0;
};

/**
 * Dynamic FlexBuffer table widget.
 *
 * Expected payload shape:
 * {
 *   "rows": [
 *     {"id": 1, "symbol": "AAPL", "price": 180.25}
 *   ]
 * }
 *
 * Columns are configured by map key, so the producer can add fields without
 * changing the C++ row type. Decode happens only when Sync() receives a new
 * binary frame, never while ImGui is traversing the active mailbox slot.
 */
template <std::size_t MaxBinarySize>
class FlexbufferTableWidget {
public:
    struct Column {
        std::string key;
        std::string header;
        float width = 0.0f;
    };

    struct Row {
        std::vector<std::string> columns;
        ImGuiID selectionId = 0;
    };

    using Mailbox = LockFreeFlexbufferMailbox<MaxBinarySize>;

    FlexbufferTableWidget() {
        static int counter = 0;
        m_tableId = "FlexbufferTable##" + std::to_string(counter++);
    }

    Mailbox& MailboxRef() { return m_mailbox; }

    void AddColumn(std::string key, std::string header, float width = 0.0f) {
        m_columns.push_back(Column{std::move(key), std::move(header), width});
    }

    bool Publish(const std::uint8_t* data, std::size_t size) { return m_mailbox.Publish(data, size); }
    bool Publish(const flexbuffers::Builder& builder) { return m_mailbox.Publish(builder); }

    void EnableFilter(bool enable = true) { m_filterEnabled = enable; }
    void EnableSelection(bool enable = true) { m_selectionEnabled = enable; }

    void Sync() {
        if (!m_mailbox.Sync()) {
            return;
        }
        const auto root = m_mailbox.Root();
        if (!root.IsMap()) {
            return;
        }

        const auto rowsRef = root.AsMap()["rows"];
        if (!rowsRef.IsVector()) {
            m_rows.clear();
            return;
        }

        std::vector<Row> decoded;
        const auto rows = rowsRef.AsVector();
        decoded.reserve(rows.size());
        for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const auto rowRef = rows[rowIndex];
            if (!rowRef.IsMap()) {
                continue;
            }

            const auto map = rowRef.AsMap();
            Row row;
            row.columns.reserve(m_columns.size());
            for (const auto& column : m_columns) {
                row.columns.push_back(ScalarToString(map[column.key.c_str()]));
            }
            row.selectionId = SelectionId(map["id"], rowIndex);
            decoded.push_back(std::move(row));
        }

        ApplySort(decoded);
        m_rows = std::move(decoded);
    }

    std::size_t GetRowCount() const { return m_rows.size(); }
    std::size_t MissedCount() const { return m_mailbox.MissedCount(); }
    const std::string& GetCell(std::size_t row, std::size_t column) const { return m_rows.at(row).columns.at(column); }

    void Render() {
        if (m_filterEnabled) {
            ImGui::InputText(("Filter##" + m_tableId).c_str(), m_filterBuffer, sizeof(m_filterBuffer));
            ImGui::SameLine();
            if (ImGui::Button(("Clear##" + m_tableId).c_str())) {
                m_filterBuffer[0] = '\0';
            }
        }

        std::vector<int> visible;
        visible.reserve(m_rows.size());
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
            if (!m_filterEnabled || m_filterBuffer[0] == '\0' || RowMatches(m_rows[i])) {
                visible.push_back(i);
            }
        }

        ImGui::Text("%zu rows", visible.size());
        if (m_mailbox.MissedCount() > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu binary frames dropped)", m_mailbox.MissedCount());
        }

        if (!ImGui::BeginTable(m_tableId.c_str(), static_cast<int>(m_columns.size()),
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_SizingFixedFit)) {
            return;
        }

        for (const auto& column : m_columns) {
            ImGui::TableSetupColumn(column.header.c_str(), ImGuiTableColumnFlags_None, column.width);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        CaptureSortSpecs();

        ImGuiMultiSelectIO* multiSelect = nullptr;
        if (m_selectionEnabled) {
            multiSelect = ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape |
                                                      ImGuiMultiSelectFlags_ClearOnClickVoid |
                                                      ImGuiMultiSelectFlags_BoxSelect1d,
                                                  m_selection.Size,
                                                  static_cast<int>(visible.size()));
            m_selection.ApplyRequests(multiSelect);
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int displayIndex = clipper.DisplayStart; displayIndex < clipper.DisplayEnd; ++displayIndex) {
                const int rowIndex = visible[displayIndex];
                const auto& row = m_rows[rowIndex];
                ImGui::TableNextRow();
                for (std::size_t columnIndex = 0; columnIndex < row.columns.size(); ++columnIndex) {
                    ImGui::TableSetColumnIndex(static_cast<int>(columnIndex));
                    if (m_selectionEnabled && columnIndex == 0) {
                        ImGui::SetNextItemSelectionUserData(row.selectionId);
                        const bool selected = m_selection.Contains(row.selectionId);
                        const std::string label = "##flexrow" + std::to_string(displayIndex);
                        ImGui::Selectable(label.c_str(), selected,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                        ImGui::SameLine(0.0f, 0.0f);
                    }
                    ImGui::TextUnformatted(row.columns[columnIndex].c_str());
                }
            }
        }

        if (m_selectionEnabled && multiSelect) {
            multiSelect = ImGui::EndMultiSelect();
            m_selection.ApplyRequests(multiSelect);
        }
        ImGui::EndTable();
    }

private:
    static std::string ScalarToString(const flexbuffers::Reference& value) {
        if (value.IsString()) return value.AsString().str();
        if (value.IsBool()) return value.AsBool() ? "Yes" : "No";
        if (value.IsInt()) return std::to_string(value.AsInt64());
        if (value.IsUInt()) return std::to_string(value.AsUInt64());
        if (value.IsFloat()) return std::to_string(value.AsDouble());
        return {};
    }

    static ImGuiID SelectionId(const flexbuffers::Reference& value, std::size_t fallback) {
        if (value.IsInt()) return static_cast<ImGuiID>(value.AsInt64());
        if (value.IsUInt()) return static_cast<ImGuiID>(value.AsUInt64());
        if (value.IsString()) return HashSelectionString(value.AsString());
        return static_cast<ImGuiID>(fallback + 1);
    }

    static ImGuiID HashSelectionString(const flexbuffers::String& value) {
        std::uint32_t hash = 2166136261u;
        for (const char* p = value.c_str(); *p != '\0'; ++p) {
            hash ^= static_cast<std::uint8_t>(*p);
            hash *= 16777619u;
        }
        return hash == 0 ? 1u : hash;
    }

    bool RowMatches(const Row& row) const {
        const std::string needle = m_filterBuffer;
        for (const auto& cell : row.columns) {
            if (cell.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    void CaptureSortSpecs() {
        if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount > 0) {
            m_sortColumn = specs->Specs[0].ColumnIndex;
            m_sortAscending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            specs->SpecsDirty = false;
        }
    }

    void ApplySort(std::vector<Row>& rows) const {
        if (m_sortColumn < 0 || m_sortColumn >= static_cast<int>(m_columns.size())) return;
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& lhs, const Row& rhs) {
            const auto& left = lhs.columns[static_cast<std::size_t>(m_sortColumn)];
            const auto& right = rhs.columns[static_cast<std::size_t>(m_sortColumn)];
            return m_sortAscending ? left < right : left > right;
        });
    }

    Mailbox m_mailbox;
    std::vector<Column> m_columns;
    std::vector<Row> m_rows;
    std::string m_tableId;
    char m_filterBuffer[256] = "";
    bool m_filterEnabled = false;
    bool m_selectionEnabled = false;
    ImGuiSelectionBasicStorage m_selection;
    int m_sortColumn = -1;
    bool m_sortAscending = true;
};

} // namespace db
