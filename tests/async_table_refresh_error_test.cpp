#include "database/async_table_widget.h"

#include <stdexcept>
#include <cstdint>
#include <string>
#include <vector>
#include <any>

int main() {
    db::AsyncTableWidget widget;
    widget.AddColumn("ID");

    std::vector<db::AsyncTableWidget::Row> initial;
    initial.push_back(db::AsyncTableWidget::Row{{"stable"}});
    widget.SetData(std::move(initial));

    int errorCount = 0;
    std::string errorMessage;
    widget.SetRefreshErrorCallback([&](const std::string& message) {
        ++errorCount;
        errorMessage = message;
    });
    widget.SetRefreshCallback([](auto&) {
        throw std::runtime_error("simulated refresh failure");
    });

    widget.Refresh();

    if (errorCount != 1 || errorMessage != "simulated refresh failure") {
        return 1;
    }
    if (widget.GetRowCount() != 1) {
        return 2;
    }

    db::AsyncTableWidget mismatchedTypes;
    mismatchedTypes.AddColumn("Value");
    mismatchedTypes.SetColumnTypedExtractor(0, [](const db::AsyncTableWidget::Row& row) -> std::any {
        return row.columns[0] == "number" ? std::any(std::int64_t{1}) : std::any(std::string("text"));
    });
    mismatchedTypes.SetRefreshCallback([](auto& rows) {
        rows.push_back(db::AsyncTableWidget::Row{{"number"}});
        rows.push_back(db::AsyncTableWidget::Row{{"text"}});
    });
    mismatchedTypes.SetSort(0, ImGuiSortDirection_Ascending);
    mismatchedTypes.Refresh();
    if (mismatchedTypes.GetRowCount() != 2) {
        return 3;
    }

    return 0;
}
