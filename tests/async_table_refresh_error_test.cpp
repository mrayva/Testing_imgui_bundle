#include "database/async_table_widget.h"

#include <stdexcept>
#include <string>
#include <vector>

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
    return 0;
}
