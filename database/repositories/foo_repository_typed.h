#pragma once

#include "../database_manager.h"
#include "../schemas/table_foo.h"
#include <sqlpp23/sqlpp23.h>
#include <vector>
#include <string>
#include <optional>

using namespace sqlpp;

/**
 * @brief FooRepository - Works exclusively with sqlpp23 types
 * 
 * Returns sqlpp23 result containers directly, no intermediate structs.
 * Conversions to strings happen in the UI layer, not here.
 */
class FooRepositoryTyped {
public:
    // Type alias for the result of SelectAll query
    using SelectAllResult = decltype(
        std::declval<sqlpp::sqlite3::connection&>()(
            select(all_of(test_db::foo)).from(test_db::foo)
        )
    );
    
    // The row type from the result
    using RowType = typename SelectAllResult::value_type;

    explicit FooRepositoryTyped(DatabaseManager& db) : m_db(db) {}

    // Create the foo table
    bool CreateTable() {
        if (!m_db.IsInitialized()) {
            return false;
        }
        
        try {
            m_db.GetConnection()("CREATE TABLE IF NOT EXISTS foo (id BIGINT, name TEXT, has_fun BOOLEAN)");
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }

    // Insert a row
    bool Insert(int64_t id, const std::string& name, bool hasFun = true) {
        if (!m_db.IsInitialized()) {
            return false;
        }
        
        try {
            m_db.GetConnection()(
                insert_into(test_db::foo).set(
                    test_db::foo.Id = id, 
                    test_db::foo.Name = name, 
                    test_db::foo.HasFun = hasFun
                )
            );
            m_lastError.clear();
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }

    /**
     * @brief Select all rows - returns sqlpp23 result container
     * 
     * Usage:
     *   auto results = repo.SelectAll();
     *   for (const auto& row : results) {
     *       int64_t id = row.Id;              // Typed access
     *       std::string name = row.Name;      // No conversion needed
     *       bool hasFun = row.HasFun;
     *   }
     * 
     * Can be used directly with AsyncTableWidgetTyped<RowType>
     */
    SelectAllResult SelectAll() {
        return m_db.GetConnection()(
            select(all_of(test_db::foo))
            .from(test_db::foo)
        );
    }

    /**
     * @brief Select by ID - returns single row or empty result
     * 
     * Note: sqlpp23 returns a result container, even for single row.
     * Check if empty to see if row was found.
     */
    auto SelectById(int64_t id) {
        return m_db.GetConnection()(
            select(all_of(test_db::foo))
            .from(test_db::foo)
            .where(test_db::foo.Id == id)
        );
    }

    // Update a row
    bool Update(int64_t id, const std::string& name, bool hasFun) {
        if (!m_db.IsInitialized()) {
            return false;
        }
        
        try {
            m_db.GetConnection()(
                update(test_db::foo)
                .set(
                    test_db::foo.Name = name,
                    test_db::foo.HasFun = hasFun
                )
                .where(test_db::foo.Id == id)
            );
            m_lastError.clear();
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }

    // Seed with initial demo data
    bool SeedDemoData() {
        return Insert(1, "Initial User", true);
    }

    // Error handling
    std::string GetLastError() const { return m_lastError; }
    bool HasError() const { return !m_lastError.empty(); }
    void ClearError() { m_lastError.clear(); }

private:
    DatabaseManager& m_db;
    std::string m_lastError;
};
