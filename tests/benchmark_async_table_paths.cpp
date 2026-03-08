#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <sqlpp23/sqlite3/sqlite3.h>

#include "database/async_table_widget.h"
#include "database/market_data_multi_index_table_model.h"

namespace {

struct BenchRow {
    std::int64_t id{};
    std::string symbol;
    std::string venue;
    std::int64_t ts{};
    double price{};
};

struct QuerySpec {
    std::string symbol;
    std::string venue;
    std::int64_t minTs{};
    std::int64_t maxTs{};
    std::size_t limit{};
    std::size_t offset{};
};

void CheckSqlite(int rc, sqlite3* db, const char* where) {
    if (rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE) {
        return;
    }
    std::string msg = std::string(where) + ": " + sqlite3_errmsg(db);
    throw std::runtime_error(msg);
}

void BuildRowsFromSqlite(sqlpp::sqlite3::connection& conn,
                         const QuerySpec& query,
                         std::vector<db::AsyncTableWidget::Row>& out) {
    out.clear();
    sqlite3* db = conn.native_handle();

    static constexpr const char* kSql =
        "SELECT id, symbol, venue, ts, price "
        "FROM market_ticks "
        "WHERE symbol = ?1 AND venue = ?2 AND ts >= ?3 AND ts <= ?4 "
        "ORDER BY ts DESC LIMIT ?5 OFFSET ?6;";

    sqlite3_stmt* stmt = nullptr;
    CheckSqlite(sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr), db, "sqlite3_prepare_v2");

    CheckSqlite(sqlite3_bind_text(stmt, 1, query.symbol.c_str(), -1, SQLITE_TRANSIENT), db, "bind symbol");
    CheckSqlite(sqlite3_bind_text(stmt, 2, query.venue.c_str(), -1, SQLITE_TRANSIENT), db, "bind venue");
    CheckSqlite(sqlite3_bind_int64(stmt, 3, query.minTs), db, "bind minTs");
    CheckSqlite(sqlite3_bind_int64(stmt, 4, query.maxTs), db, "bind maxTs");
    CheckSqlite(sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(query.limit)), db, "bind limit");
    CheckSqlite(sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(query.offset)), db, "bind offset");

    while (true) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) {
            break;
        }
        CheckSqlite(rc, db, "sqlite3_step");

        const auto id = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 0));
        const char* symbolPtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* venuePtr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto ts = static_cast<std::int64_t>(sqlite3_column_int64(stmt, 3));
        const double price = sqlite3_column_double(stmt, 4);

        db::MarketDataTypedData typed{id,
                                      symbolPtr ? symbolPtr : "",
                                      venuePtr ? venuePtr : "",
                                      ts,
                                      price};
        out.push_back(db::AsyncTableWidget::Row{{std::to_string(id),
                                                 typed.symbol,
                                                 typed.venue,
                                                 std::to_string(ts),
                                                 std::to_string(price)},
                                                typed});
    }

    CheckSqlite(sqlite3_finalize(stmt), db, "sqlite3_finalize");
}

void SeedSqlite(sqlpp::sqlite3::connection& conn, const std::vector<BenchRow>& rows) {
    conn("CREATE TABLE market_ticks ("
         "id INTEGER PRIMARY KEY, "
         "symbol TEXT NOT NULL, "
         "venue TEXT NOT NULL, "
         "ts BIGINT NOT NULL, "
         "price REAL NOT NULL)");
    conn("CREATE INDEX idx_market_ticks_symbol_venue_ts ON market_ticks(symbol, venue, ts)");

    sqlite3* db = conn.native_handle();
    CheckSqlite(sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr), db, "BEGIN");

    sqlite3_stmt* stmt = nullptr;
    static constexpr const char* kInsertSql =
        "INSERT INTO market_ticks(id, symbol, venue, ts, price) VALUES(?1, ?2, ?3, ?4, ?5);";
    CheckSqlite(sqlite3_prepare_v2(db, kInsertSql, -1, &stmt, nullptr), db, "prepare insert");

    for (const auto& row : rows) {
        CheckSqlite(sqlite3_bind_int64(stmt, 1, row.id), db, "bind id");
        CheckSqlite(sqlite3_bind_text(stmt, 2, row.symbol.c_str(), -1, SQLITE_TRANSIENT), db, "bind symbol");
        CheckSqlite(sqlite3_bind_text(stmt, 3, row.venue.c_str(), -1, SQLITE_TRANSIENT), db, "bind venue");
        CheckSqlite(sqlite3_bind_int64(stmt, 4, row.ts), db, "bind ts");
        CheckSqlite(sqlite3_bind_double(stmt, 5, row.price), db, "bind price");
        CheckSqlite(sqlite3_step(stmt), db, "step insert");
        CheckSqlite(sqlite3_reset(stmt), db, "reset insert");
        CheckSqlite(sqlite3_clear_bindings(stmt), db, "clear bindings");
    }

    CheckSqlite(sqlite3_finalize(stmt), db, "finalize insert");
    CheckSqlite(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr), db, "COMMIT");
}

std::vector<BenchRow> GenerateRows(std::size_t count) {
    static const std::vector<std::string> symbols = {
        "AAPL", "MSFT", "NVDA", "AMZN", "META", "TSLA", "GOOG", "AMD",
        "INTC", "NFLX", "ORCL", "CRM", "ADBE", "QCOM", "AVGO", "SHOP"};
    static const std::vector<std::string> venues = {"XNAS", "XNYS", "BATS", "IEX"};

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> symbolDist(0, static_cast<int>(symbols.size() - 1));
    std::uniform_int_distribution<int> venueDist(0, static_cast<int>(venues.size() - 1));
    std::uniform_real_distribution<double> priceDist(10.0, 1000.0);

    std::vector<BenchRow> rows;
    rows.reserve(count);
    const std::int64_t baseTs = 1'700'000'000'000;

    for (std::size_t i = 0; i < count; ++i) {
        rows.push_back(BenchRow{static_cast<std::int64_t>(i + 1),
                                symbols[symbolDist(rng)],
                                venues[venueDist(rng)],
                                baseTs + static_cast<std::int64_t>(i * 10),
                                priceDist(rng)});
    }
    return rows;
}

template <typename Fn>
std::vector<double> RunTimed(Fn&& fn, int iterations, std::uint64_t& checksum) {
    std::vector<double> ms;
    ms.reserve(static_cast<std::size_t>(iterations));
    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::steady_clock::now();
        checksum += fn();
        auto end = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    return ms;
}

void PrintStats(const char* label, const std::vector<double>& samples) {
    const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
    const double avg = total / static_cast<double>(samples.size());
    const auto [minIt, maxIt] = std::minmax_element(samples.begin(), samples.end());
    std::cout << label << " avg_ms=" << avg << " min_ms=" << *minIt << " max_ms=" << *maxIt
              << " n=" << samples.size() << "\n";
}

} // namespace

int main() {
    try {
        constexpr std::size_t kRows = 200000;
        constexpr int kWarmupIters = 3;
        constexpr int kMeasureIters = 20;

        auto rows = GenerateRows(kRows);

        db::MarketDataMultiIndexTableModel model(kRows + 1000);
        for (const auto& row : rows) {
            model.Upsert(db::MarketDataCacheEntry{row.id, row.symbol, row.venue, row.ts, row.price});
        }

        sqlpp::sqlite3::connection_config cfg;
        cfg.path_to_database = ":memory:";
        cfg.flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        sqlpp::sqlite3::connection conn(cfg);
        SeedSqlite(conn, rows);

        QuerySpec spec{
            .symbol = "NVDA",
            .venue = "XNAS",
            .minTs = rows.front().ts,
            .maxTs = rows.back().ts,
            .limit = 2000,
            .offset = 0,
        };

        db::MarketDataMultiIndexTableModel::Query miQuery;
        miQuery.symbolEq = spec.symbol;
        miQuery.venueEq = spec.venue;
        miQuery.minTs = spec.minTs;
        miQuery.maxTs = spec.maxTs;
        miQuery.order = db::MarketDataMultiIndexTableModel::Order::TsDesc;
        miQuery.limit = spec.limit;
        miQuery.offset = spec.offset;

        std::vector<db::AsyncTableWidget::Row> out;
        std::uint64_t checksum = 0;

        for (int i = 0; i < kWarmupIters; ++i) {
            BuildRowsFromSqlite(conn, spec, out);
            checksum += out.size();
            model.BuildAsyncRows(out, miQuery);
            checksum += out.size();
        }

        auto sqliteMs = RunTimed(
            [&]() -> std::uint64_t {
                BuildRowsFromSqlite(conn, spec, out);
                return static_cast<std::uint64_t>(out.size() * 13 + (out.empty() ? 0 : out.front().columns[0].size()));
            },
            kMeasureIters,
            checksum);

        auto multiIndexMs = RunTimed(
            [&]() -> std::uint64_t {
                model.BuildAsyncRows(out, miQuery);
                return static_cast<std::uint64_t>(out.size() * 17 + (out.empty() ? 0 : out.front().columns[0].size()));
            },
            kMeasureIters,
            checksum);

        PrintStats("sqlite_async_path", sqliteMs);
        PrintStats("multi_index_path", multiIndexMs);

        const double sqliteAvg = std::accumulate(sqliteMs.begin(), sqliteMs.end(), 0.0) / sqliteMs.size();
        const double miAvg = std::accumulate(multiIndexMs.begin(), multiIndexMs.end(), 0.0) / multiIndexMs.size();
        std::cout << "speedup_x=" << (sqliteAvg / miAvg) << " rows_total=" << kRows << " checksum=" << checksum
                  << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "benchmark failed: " << e.what() << "\n";
        return 1;
    }
}
