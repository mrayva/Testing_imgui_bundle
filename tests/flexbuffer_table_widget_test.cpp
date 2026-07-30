#include "database/flexbuffer_table_widget.h"

#include <array>
#include <cstdint>

int main() {
    db::FlexbufferTableWidget<1024> widget;
    widget.AddColumn("id", "ID");
    widget.AddColumn("symbol", "Symbol");
    widget.AddColumn("price", "Price");

    flexbuffers::Builder builder;
    builder.Map([&]() {
        builder.Vector("rows", [&]() {
            builder.Map([&]() {
                builder.Int("id", 7);
                builder.String("symbol", "AAPL");
                builder.Double("price", 181.25);
            });
            builder.Map([&]() {
                builder.Int("id", 8);
                builder.String("symbol", "MSFT");
                builder.Double("price", 412.50);
            });
        });
    });
    builder.Finish();

    if (!widget.Publish(builder)) return 1;
    widget.Sync();
    if (widget.GetRowCount() != 2) return 2;
    if (widget.GetCell(0, 0) != "7" || widget.GetCell(0, 1) != "AAPL") return 3;

    std::array<std::uint8_t, 1025> oversized{};
    if (widget.Publish(oversized.data(), oversized.size())) return 4;
    widget.Sync();
    if (widget.MissedCount() == 0) return 5;
    return 0;
}
