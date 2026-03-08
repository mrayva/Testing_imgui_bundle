#pragma once

#include <algorithm>
#include <any>
#include <cctype>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>
#include <mutex>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>

#include <multi_index_lru/container.hpp>

#include "async_table_widget.h"

namespace db {

struct FooCacheEntry {
    std::int64_t id{};
    std::string name;
    bool hasFun{};
};

struct FooByIdTag {};
struct FooByNameTag {};
struct FooByHasFunTag {};
struct FooByNameHasFunTag {};

using FooCache = multi_index_lru::Container<
    FooCacheEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            boost::multi_index::tag<FooByIdTag>,
            boost::multi_index::member<FooCacheEntry, std::int64_t, &FooCacheEntry::id>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<FooByNameTag>,
            boost::multi_index::member<FooCacheEntry, std::string, &FooCacheEntry::name>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<FooByHasFunTag>,
            boost::multi_index::member<FooCacheEntry, bool, &FooCacheEntry::hasFun>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<FooByNameHasFunTag>,
            boost::multi_index::composite_key<
                FooCacheEntry,
                boost::multi_index::member<FooCacheEntry, std::string, &FooCacheEntry::name>,
                boost::multi_index::member<FooCacheEntry, bool, &FooCacheEntry::hasFun>>>>>;

struct FooTypedData {
    std::int64_t id{};
    std::string name;
    bool hasFun{};
};

class FooMultiIndexTableModel {
public:
    enum class Order {
        LruMostRecentFirst = 0,
        IdAsc,
        IdDesc,
        NameAsc,
        NameDesc
    };

    struct Query {
        std::optional<bool> hasFunFilter;
        std::string namePrefix;
        std::string textContains;
        Order order = Order::LruMostRecentFirst;
        std::size_t offset = 0;
        std::size_t limit = 0;
    };

    explicit FooMultiIndexTableModel(std::size_t capacity) : cache_(capacity) {}

    bool Upsert(FooCacheEntry row) {
        std::unique_lock lock(mutex_);
        cache_.erase<FooByIdTag>(row.id);
        return cache_.insert(std::move(row));
    }

    bool EraseById(std::int64_t id) {
        std::unique_lock lock(mutex_);
        return cache_.erase<FooByIdTag>(id);
    }

    std::optional<FooCacheEntry> FindByIdNoUpdate(std::int64_t id) const {
        std::shared_lock lock(mutex_);
        auto it = cache_.find_no_update<FooByIdTag>(id);
        if (it == cache_.end<FooByIdTag>()) {
            return std::nullopt;
        }
        return *it;
    }

    std::size_t Size() const {
        std::shared_lock lock(mutex_);
        return cache_.size();
    }

    static void ConfigureAsyncTableColumns(AsyncTableWidget& table) {
        table.AddColumn("ID", 80.0f);
        table.AddColumn("Name", 220.0f);
        table.AddColumn("Has Fun", 100.0f);
        table.EnableFilter(true);
        table.EnableSelection(true);

        table.SetColumnTypedExtractor(0, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<FooTypedData>(&row.userData)) {
                return std::any(data->id);
            }
            return {};
        });
        table.SetColumnTypedExtractor(1, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<FooTypedData>(&row.userData)) {
                return std::any(data->name);
            }
            return {};
        });
        table.SetColumnTypedExtractor(2, [](const AsyncTableWidget::Row& row) -> std::any {
            if (const auto* data = std::any_cast<FooTypedData>(&row.userData)) {
                return std::any(data->hasFun);
            }
            return {};
        });
    }

    void BuildAsyncRows(std::vector<AsyncTableWidget::Row>& out, const Query& query) const {
        out.clear();
        std::shared_lock lock(mutex_);

        std::size_t seen = 0;
        std::size_t emitted = 0;
        auto pushIfMatch = [&](const FooCacheEntry& entry) {
            if (query.hasFunFilter && entry.hasFun != *query.hasFunFilter) {
                return;
            }
            if (!query.namePrefix.empty() && !StartsWith(entry.name, query.namePrefix)) {
                return;
            }
            if (!query.textContains.empty() && !ContainsCaseInsensitive(entry.name, query.textContains)) {
                return;
            }
            if (seen++ < query.offset) {
                return;
            }
            if (query.limit != 0 && emitted >= query.limit) {
                return;
            }

            out.push_back(AsyncTableWidget::Row{
                {std::to_string(entry.id), entry.name, entry.hasFun ? "Yes" : "No"},
                FooTypedData{entry.id, entry.name, entry.hasFun}
            });
            ++emitted;
        };

        switch (query.order) {
            case Order::LruMostRecentFirst: {
                const auto& lru = cache_.get_container().template get<0>();
                for (const auto& entry : lru) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::IdAsc: {
                const auto& idx = cache_.get_container().template get<FooByIdTag>();
                for (const auto& entry : idx) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::IdDesc: {
                const auto& idx = cache_.get_container().template get<FooByIdTag>();
                for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                    pushIfMatch(*it);
                }
                break;
            }
            case Order::NameAsc: {
                const auto& idx = cache_.get_container().template get<FooByNameTag>();
                for (const auto& entry : idx) {
                    pushIfMatch(entry);
                }
                break;
            }
            case Order::NameDesc: {
                const auto& idx = cache_.get_container().template get<FooByNameTag>();
                for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
                    pushIfMatch(*it);
                }
                break;
            }
        }
    }

private:
    static bool StartsWith(const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
    }

    static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };

        std::string lhs(haystack.size(), '\0');
        std::string rhs(needle.size(), '\0');
        std::transform(haystack.begin(), haystack.end(), lhs.begin(), lower);
        std::transform(needle.begin(), needle.end(), rhs.begin(), lower);
        return lhs.find(rhs) != std::string::npos;
    }

    mutable std::shared_mutex mutex_;
    FooCache cache_;
};

} // namespace db
