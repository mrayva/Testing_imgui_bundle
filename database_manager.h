#pragma once

#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>
#include "table_foo.h"

#include <string>
#include <vector>
#include <memory>
#include <iostream>

class DatabaseManager {
public:
    static DatabaseManager& Get() {
        static DatabaseManager instance;
        return instance;
    }

    bool Initialize(const std::string& path = ":memory:") {
        try {
            sqlpp::sqlite3::connection_config config;
            config.path_to_database = path;
            config.flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
            m_db = std::make_unique<sqlpp::sqlite3::connection>(config);

           // Create table
            (*m_db)("CREATE TABLE foo (id BIGINT, name TEXT, has_fun BOOLEAN)");
        
            // Seed with some initial data
            (*m_db)(insert_into(test_db::foo).set(test_db::foo.Id = 1, test_db::foo.Name = "Initial User", test_db::foo.HasFun = true));
            
            m_lastError.clear();
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }

    bool InsertRow(int64_t id, const std::string& name, bool hasFun = true) {
        if (!m_db) return false;
        try {
            (*m_db)(insert_into(test_db::foo).set(
                test_db::foo.Id = id, 
                test_db::foo.Name = name, 
                test_db::foo.HasFun = hasFun));
            m_lastError.clear();
            return true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }

    std::vector<std::string> SelectAllRows() {
        std::vector<std::string> results;
        if (!m_db) return results;
        try {
            // Added .unconditionally()
            for (const auto& row : (*m_db)(select(test_db::foo.Id, test_db::foo.Name).from(test_db::foo))) {
                results.push_back("ID: " + std::to_string(row.Id) + ", Name: " + std::string(row.Name));
            }
            m_lastError.clear();
        } catch (const std::exception& e) {
            m_lastError = e.what();
        }
        return results;
    }

    std::string GetLastError() const { return m_lastError; }
    bool HasError() const { return !m_lastError.empty(); }
    void ClearError() { m_lastError.clear(); }
    bool IsInitialized() const { return m_db != nullptr; }

private:
    std::unique_ptr<sqlpp::sqlite3::connection> m_db;
    std::string m_lastError;
};
