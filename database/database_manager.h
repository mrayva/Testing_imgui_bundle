#pragma once

#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>
#include <string>
#include <memory>
#include <iostream>
#include "database_mode.h"

class DatabaseManager {
public:
    static DatabaseManager& Get() {
        static DatabaseManager instance;
        return instance;
    }

    bool Initialize(const DatabaseConfig& config = DatabaseConfig::Memory()) {
        try {
            sqlpp::sqlite3::connection_config conn_config;
            
            // Determine database path based on mode
            switch (config.mode) {
                case DatabaseMode::Memory:
                    conn_config.path_to_database = ":memory:";
                    break;
                    
                case DatabaseMode::NativeFile:
                    if (config.path.empty()) {
                        m_lastError = "NativeFile mode requires a path";
                        return false;
                    }
                    conn_config.path_to_database = config.path;
                    break;
                    
                case DatabaseMode::OPFS:
#ifdef __EMSCRIPTEN__
                    // OPFS mode for WebAssembly
                    // Path should be something like "/opfs/database.db"
                    if (config.path.empty()) {
                        m_lastError = "OPFS mode requires a path";
                        return false;
                    }
                    conn_config.path_to_database = config.path;
#else
                    m_lastError = "OPFS mode is only available in WebAssembly builds";
                    return false;
#endif
                    break;
            }
            
            // Set appropriate flags
            if (config.create_if_missing) {
                conn_config.flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
            } else {
                conn_config.flags = SQLITE_OPEN_READWRITE;
            }
            
            m_db = std::make_unique<sqlpp::sqlite3::connection>(conn_config);
            m_lastError.clear();
            m_currentMode = config.mode;
            
            // Apply performance tuning
            if (config.tuning.enabled) {
                ApplyPerformanceTuning(config.tuning);
            }
            
            return true;
            
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return false;
        }
    }

    // Get direct access to connection for custom queries
    sqlpp::sqlite3::connection& GetConnection() {
        if (!m_db) {
            throw std::runtime_error("Database not initialized. Call Initialize() first.");
        }
        return *m_db;
    }

    // Check if connection is valid
    bool IsInitialized() const { 
        return m_db != nullptr; 
    }
    
    // Get current mode
    DatabaseMode GetMode() const {
        return m_currentMode;
    }

    // Error handling
    std::string GetLastError() const { return m_lastError; }
    bool HasError() const { return !m_lastError.empty(); }
    void ClearError() { m_lastError.clear(); }

    // Apply performance tuning manually (optional)
    void ApplyPerformanceTuning(const PerformanceTuning& tuning) {
        if (!m_db) {
            throw std::runtime_error("Database not initialized");
        }
        
        try {
            // Note: page_size must be set BEFORE any tables are created
            (*m_db)("PRAGMA page_size = " + std::to_string(tuning.page_size) + ";");
            (*m_db)("PRAGMA journal_mode = " + tuning.journal_mode + ";");
            (*m_db)("PRAGMA synchronous = " + tuning.synchronous + ";");
            (*m_db)("PRAGMA cache_size = -" + std::to_string(tuning.cache_size_kb) + ";");
            (*m_db)("PRAGMA temp_store = " + tuning.temp_store + ";");
        } catch (const std::exception& e) {
            m_lastError = std::string("Performance tuning failed: ") + e.what();
        }
    }

private:
    DatabaseManager() = default;
    ~DatabaseManager() = default;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    std::unique_ptr<sqlpp::sqlite3::connection> m_db;
    std::string m_lastError;
    DatabaseMode m_currentMode = DatabaseMode::Memory;
};
