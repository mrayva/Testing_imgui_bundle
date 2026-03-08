#include "database/async_table_widget.h"
#include "database/reactive_list_widget.h"

#include <algorithm>
#include <map>
#include <vector>

static bool Contains(const std::vector<int>& values, int target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

struct FakeCollectionRow {
    double lastElem1{};
    long lastElem2{};
};

struct FakeCollection {
    using elem1_type = double;
    using elem2_type = long;
    using total1_type = long;
    using total2_type = double;
    using id_type = std::size_t;
    using storage_type = std::map<id_type, FakeCollectionRow>;
    using const_iterator = storage_type::const_iterator;

    storage_type data;

    std::size_t size() const { return data.size(); }
    total1_type total1() const {
        total1_type total = 0;
        for (const auto& [id, row] : data) {
            (void)id;
            total += row.lastElem2;
        }
        return total;
    }
    total2_type total2() const {
        total2_type total = 0;
        for (const auto& [id, row] : data) {
            (void)id;
            total += row.lastElem1 * static_cast<double>(row.lastElem2);
        }
        return total;
    }
    const_iterator begin() const { return data.begin(); }
    const_iterator end() const { return data.end(); }
};

int main() {
    db::AsyncTableWidget asyncWidget;
    asyncWidget.AddColumn("ID");
    asyncWidget.AddColumn("Value");
    asyncWidget.EnableSelection(true);

    std::vector<db::AsyncTableWidget::Row> firstRows;
    db::AsyncTableWidget::Row row1;
    row1.columns = {"1", "A"};
    firstRows.push_back(row1);
    db::AsyncTableWidget::Row row2;
    row2.columns = {"2", "B"};
    firstRows.push_back(row2);
    db::AsyncTableWidget::Row row3;
    row3.columns = {"3", "C"};
    firstRows.push_back(row3);
    asyncWidget.SetData(std::move(firstRows));

    asyncWidget.GetSelection().SetItemSelected((ImGuiID)2, true);
    auto asyncSelected = asyncWidget.GetSelectedIndices();
    if (!Contains(asyncSelected, 2)) {
        return 1;
    }

    std::vector<db::AsyncTableWidget::Row> secondRows;
    db::AsyncTableWidget::Row row4;
    row4.columns = {"3", "C"};
    secondRows.push_back(row4);
    db::AsyncTableWidget::Row row5;
    row5.columns = {"1", "A"};
    secondRows.push_back(row5);
    db::AsyncTableWidget::Row row6;
    row6.columns = {"2", "B"};
    secondRows.push_back(row6);
    asyncWidget.SetData(std::move(secondRows));

    asyncSelected = asyncWidget.GetSelectedIndices();
    if (!Contains(asyncSelected, 2)) {
        return 2;
    }

    FakeCollection collection;
    collection.data.emplace(1, FakeCollectionRow{10.0, 10});
    collection.data.emplace(2, FakeCollectionRow{20.0, 20});
    collection.data.emplace(3, FakeCollectionRow{30.0, 30});

    db::ReactiveListWidget<FakeCollection> reactiveWidget;
    reactiveWidget.EnableSelection(true);
    reactiveWidget.SetSort(0, ImGuiSortDirection_Descending); // Sort by ID descending.
    reactiveWidget.Refresh(collection);
    reactiveWidget.GetSelection().SetItemSelected((ImGuiID)1, true);

    auto reactiveSelected = reactiveWidget.GetSelectedIndices();
    if (reactiveSelected.size() != 1) {
        return 3;
    }
    if (reactiveSelected[0] != 2) {
        return 4;
    }

    reactiveWidget.GetSelection().SetItemSelected((ImGuiID)1, false);
    reactiveWidget.GetSelection().SetItemSelected((ImGuiID)3, true);
    reactiveSelected = reactiveWidget.GetSelectedIndices();
    if (reactiveSelected.size() != 1 || reactiveSelected[0] != 0) {
        return 5;
    }

    return 0;
}
