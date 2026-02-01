#pragma once

#include "../database_manager.h"
#include "../schemas/table_foo.h"
#include <sqlpp23/sqlpp23.h>
#include <vector>
#include <string>
#include <optional>

using namespace sqlpp;

// Simple struct to hold foo row data
struct FooRow {
    int64_t id;
    std::string name;
    bool has_fun;
};

class FooRepository {
public:
    explicit FooRepository(DatabaseManager& db) : m_db(db) {}

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

    // Select all rows (returns formatted strings for GUI display)
    std::vector<std::string> SelectAllAsStrings() {
        std::vector<std::string> results;
        if (!m_db.IsInitialized()) {
            return results;
        }
        
        try {
            for (const auto& row : m_db.GetConnection()(
                select(test_db::foo.Id, test_db::foo.Name)
                .from(test_db::foo)
            )) {
                results.push_back("ID: " + std::to_string(row.Id) + ", Name: " + std::string(row.Name));
            }
            m_lastError.clear();
        } catch (const std::exception& e) {
            m_lastError = e.what();
        }
        return results;
    }

    // Select all rows (returns structured data)
    std::vector<FooRow> SelectAll() {
        std::vector<FooRow> results;
        if (!m_db.IsInitialized()) {
            return results;
        }
        
        try {
            for (const auto& row : m_db.GetConnection()(
                select(all_of(test_db::foo))
                .from(test_db::foo)
            )) {
                results.push_back(FooRow{
                    .id = row.Id,
                    .name = std::string(row.Name),
                    .has_fun = row.HasFun
                });
            }
            m_lastError.clear();
        } catch (const std::exception& e) {
            m_lastError = e.what();
        }
        return results;
    }

    // Select by ID
    std::optional<FooRow> SelectById(int64_t id) {
        if (!m_db.IsInitialized()) {
            return std::nullopt;
        }
        
        try {
            for (const auto& row : m_db.GetConnection()(
                select(all_of(test_db::foo))
                .from(test_db::foo)
                .where(test_db::foo.Id == id)
            )) {
                return FooRow{
                    .id = row.Id,
                    .name = std::string(row.Name),
                    .has_fun = row.HasFun
                };
            }
        } catch (const std::exception& e) {
            m_lastError = e.what();
        }
        return std::nullopt;
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

    // Delete a row
    // NOTE: Commented out due to gcc-15 / Emscripten "deducing this" bug in sqlpp23
    // Use raw SQL as workaround: m_db.GetConnection()("DELETE FROM foo WHERE id = ?", id);
    /*
    bool Delete(int64_t id) {
        if (!m_db.IsInitialized()) {
            return false;
        }
        
        try {
            m_db.GetConnection()(
                sqlpp::delete_from(test_db::foo)
                .where(test_db::foo.Id == id)
            );
            m_lastError.clear();
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }
    */

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
