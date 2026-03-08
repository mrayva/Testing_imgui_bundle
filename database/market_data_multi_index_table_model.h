#pragma once

#include <algorithm>
#include <any>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>

#include <multi_index_lru/container.hpp>

#include "async_table_widget.h"

namespace db {

struct MarketDataCacheEntry {
    std::int64_t id{};
    std::string symbol;
    std::string venue;
    std::int64_t ts{};
    double price{};
};

struct MdByIdTag {};
struct MdByTsTag {};
struct MdByPriceTag {};
struct MdBySymbolVenueTsTag {};
struct MdBySymbolTsTag {};

using MarketDataCache = multi_index_lru::Container<
    MarketDataCacheEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            boost::multi_index::tag<MdByIdTag>,
            boost::multi_index::member<MarketDataCacheEntry, std::int64_t, &MarketDataCacheEntry::id>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<MdByTsTag>,
            boost::multi_index::member<MarketDataCacheEntry, std::int64_t, &MarketDataCacheEntry::ts>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<MdByPriceTag>,
            boost::multi_index::member<MarketDataCacheEntry, double, &MarketDataCacheEntry::price>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<MdBySymbolVenueTsTag>,
            boost::multi_index::composite_key<
                MarketDataCacheEntry,
                boost::multi_index::member<MarketDataCacheEntry, std::string, &MarketDataCacheEntry::symbol>,
                boost::multi_index::member<MarketDataCacheEntry, std::string, &MarketDataCacheEntry::venue>,
                boost::multi_index::member<MarketDataCacheEntry, std::int64_t, &MarketDataCacheEntry::ts>>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<MdBySymbolTsTag>,
            boost::multi_index::composite_key<
                MarketDataCacheEntry,
                boost::multi_index::member<MarketDataCacheEntry, std::string, &MarketDataCacheEntry::symbol>,
                boost::multi_index::member<MarketDataCacheEntry, std::int64_t, &MarketDataCacheEntry::ts>>>>>;

struct MarketDataTypedData {
    std::int64_t id{};
    std::string symbol;
    std::string venue;
    std::int64_t ts{};
    double price{};
};

class MarketDataMultiIndexTableModel {
public:
    enum class Order {
        LruMostRecentFirst = 0,
        TsAsc,
        TsDesc,
        PriceAsc,
        PriceDesc,
        SymbolAsc,
        SymbolDesc
    };

    struct Query {
        std::optional<std::string> symbolEq;
        std::optional<std::string> venueEq;
        std::optional<std::int64_t> minTs;
        std::optional<std::int64_t> maxTs;
        std::optional<double> minPrice;
        std::optional<double> maxPrice;
        Order order = Order::TsDesc;
        std::size_t offset = 0;
        std::size_t limit = 0;
    };

    explicit MarketDataMultiIndexTableModel(std::size_t capacity) : cache_(capacity) {}

    bool Upsert(MarketDataCacheEntry row) {
        std::unique_lock lock(mutex_);
        cache_.erase<MdByIdTag>(row.id);
        return cache_.insert(std::move(row));
    }

    bool EraseById(std::int64_t id) {
        std::unique_lock lock(mutex_);
        return cache_.erase<MdByIdTag>(id);
    }

    std::size_t Size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }

    static void ConfigureAsyncTableColumns(AsyncTableWidget& table) {
        table.AddColumn("ID", 80.0f);
        table.AddColumn("Symbol", 110.0f);
        table.AddColumn("Venue", 110.0f);
        table.AddColumn("TS", 140.0f);
        table.AddColumn("Price", 100.0f);
        table.EnableFilter(true);
        table.EnableSelection(true);

        table.SetColumnTypedExtractor(0, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<MarketDataTypedData>(&row.userData)) {
                return std::any(data->id);
            }
            return {};
        });
        table.SetColumnTypedExtractor(1, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<MarketDataTypedData>(&row.userData)) {
                return std::any(data->symbol);
            }
            return {};
        });
        table.SetColumnTypedExtractor(2, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<MarketDataTypedData>(&row.userData)) {
                return std::any(data->venue);
            }
            return {};
        });
        table.SetColumnTypedExtractor(3, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<MarketDataTypedData>(&row.userData)) {
                return std::any(data->ts);
            }
            return {};
        });
        table.SetColumnTypedExtractor(4, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<MarketDataTypedData>(&row.userData)) {
                return std::any(data->price);
            }
            return {};
        });
    }

    void BuildAsyncRows(std::vector<AsyncTableWidget::Row>& out, const Query& query) const {
        out.clear();
        std::shared_lock lock(mutex_);

        std::size_t seen = 0;
        std::size_t emitted = 0;
        auto pushIfMatch = [&](const MarketDataCacheEntry& entry) {
            if (query.symbolEq && entry.symbol != *query.symbolEq) {
                return;
            }
            if (query.venueEq && entry.venue != *query.venueEq) {
                return;
            }
            if (query.minTs && entry.ts < *query.minTs) {
                return;
            }
            if (query.maxTs && entry.ts > *query.maxTs) {
                return;
            }
            if (query.minPrice && entry.price < *query.minPrice) {
                return;
            }
            if (query.maxPrice && entry.price > *query.maxPrice) {
                return;
            }
            if (seen++ < query.offset) {
                return;
            }
            if (query.limit != 0 && emitted >= query.limit) {
                return;
            }

            out.push_back(AsyncTableWidget::Row{{std::to_string(entry.id),
                                                entry.symbol,
                                                entry.venue,
                                                std::to_string(entry.ts),
                                                FormatPrice(entry.price)},
                                               MarketDataTypedData{entry.id,
                                                                   entry.symbol,
                                                                   entry.venue,
                                                                   entry.ts,
                                                                   entry.price}});
            ++emitted;
        };

        if (query.symbolEq && query.venueEq && (query.order == Order::TsAsc || query.order == Order::TsDesc)) {
            const auto& idx = cache_.get_container().template get<MdBySymbolVenueTsTag>();
            const auto minTs = query.minTs.value_or(std::numeric_limits<std::int64_t>::lowest());
            const auto maxTs = query.maxTs.value_or(std::numeric_limits<std::int64_t>::max());
            auto begin = idx.lower_bound(boost::make_tuple(*query.symbolEq, *query.venueEq, minTs));
            auto end = idx.upper_bound(boost::make_tuple(*query.symbolEq, *query.venueEq, maxTs));

            if (query.order == Order::TsAsc) {
                for (auto it = begin; it != end; ++it) {
                    pushIfMatch(*it);
                }
            } else {
                for (auto it = boost::make_reverse_iterator(end); it != boost::make_reverse_iterator(begin); ++it) {
                    pushIfMatch(*it);
                }
            }
            return;
        }

        switch (query.order) {
            case Order::LruMostRecentFirst: {
                const auto& lru = cache_.get_container().template get<0>();
                for (const auto& entry : lru) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::TsAsc: {
                const auto& idx = cache_.get_container().template get<MdByTsTag>();
                for (const auto& entry : idx) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::TsDesc: {
                const auto& idx = cache_.get_container().template get<MdByTsTag>();
                for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                    pushIfMatch(*it);
                }
                break;
            }
            case Order::PriceAsc: {
                const auto& idx = cache_.get_container().template get<MdByPriceTag>();
                for (const auto& entry : idx) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::PriceDesc: {
                const auto& idx = cache_.get_container().template get<MdByPriceTag>();
                for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                    pushIfMatch(*it);
                }
                break;
            }
            case Order::SymbolAsc: {
                const auto& idx = cache_.get_container().template get<MdBySymbolTsTag>();
                for (const auto& entry : idx) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::SymbolDesc: {
                const auto& idx = cache_.get_container().template get<MdBySymbolTsTag>();
                for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                    pushIfMatch(*it);
                }
                break;
            }
        }
    }

private:
    static std::string FormatPrice(double value) {
        return std::to_string(value);
    }

    mutable std::shared_mutex mutex_;
    MarketDataCache cache_;
};

} // namespace db
