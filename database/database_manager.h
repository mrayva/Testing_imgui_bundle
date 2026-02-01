#pragma once

#include <sqlpp23/sqlite3/sqlite3.h>
#include <sqlpp23/sqlpp23.h>
#include <string>
#include <memory>
#include <iostream>
#include <functional>
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

    // ============================================================================
    // SQLite Online Backup API
    // ============================================================================
    
    /**
     * @brief Load an entire database from disk/OPFS into the current (memory) database
     * 
     * This is the most efficient way to load a disk database into :memory: because
     * it copies raw pages directly without parsing SQL or executing INSERT statements.
     * 
     * @param source_path Path to the source database file
     * @return true if backup succeeded, false otherwise
     * 
     * @note Current database should be :memory: for best results
     * @note This is a blocking operation - use BackupFromFileIncremental for non-blocking
     * 
     * Example:
     *   DatabaseManager::Get().Initialize(DatabaseConfig::Memory());
     *   if (!DatabaseManager::Get().BackupFromFile("/opfs/data.db")) {
     *       // Handle error
     *   }
     */
    bool BackupFromFile(const std::string& source_path) {
        if (!m_db) {
            m_lastError = "Database not initialized";
            return false;
        }

        try {
            // Create temporary connection to source database
            sqlpp::sqlite3::connection_config source_config;
            source_config.path_to_database = source_path;
            source_config.flags = SQLITE_OPEN_READONLY;
            
            sqlpp::sqlite3::connection source_db(source_config);
            
            // Get raw C handles
            sqlite3* pSrc = source_db.native_handle();
            sqlite3* pDest = m_db->native_handle();
            
            if (!pSrc || !pDest) {
                m_lastError = "Failed to get database handles";
                return false;
            }
            
            // Perform backup
            sqlite3_backup* pBackup = sqlite3_backup_init(pDest, "main", pSrc, "main");
            if (!pBackup) {
                m_lastError = std::string("Backup init failed: ") + sqlite3_errmsg(pDest);
                return false;
            }
            
            // Copy entire database in one step (-1 = all pages)
            int rc = sqlite3_backup_step(pBackup, -1);
            sqlite3_backup_finish(pBackup);
            
            if (rc != SQLITE_DONE) {
                m_lastError = std::string("Backup failed: ") + sqlite3_errmsg(pDest);
                return false;
            }
            
            return true;
            
        } catch (const std::exception& e) {
            m_lastError = std::string("Backup exception: ") + e.what();
            return false;
        }
    }

    /**
     * @brief Incrementally backup from source to current database
     * 
     * This allows non-blocking database loading by copying N pages at a time.
     * Useful in WASM environments to avoid blocking the main thread.
     * 
     * @param source_path Path to source database
     * @param pages_per_step Number of pages to copy per step (-1 = all at once)
     * @param progress_callback Optional callback(remaining_pages, total_pages)
     * @return true if backup completed successfully
     * 
     * Example (non-blocking):
     *   DatabaseManager::Get().BackupFromFileIncremental("/opfs/data.db", 100, 
     *       [](int remaining, int total) {
     *           std::cout << "Progress: " << (100 * (total - remaining) / total) << "%" << std::endl;
     *       }
     *   );
     */
    bool BackupFromFileIncremental(
        const std::string& source_path,
        int pages_per_step = 100,
        std::function<void(int remaining, int total)> progress_callback = nullptr
    ) {
        if (!m_db) {
            m_lastError = "Database not initialized";
            return false;
        }

        try {
            // Create temporary connection to source database
            sqlpp::sqlite3::connection_config source_config;
            source_config.path_to_database = source_path;
            source_config.flags = SQLITE_OPEN_READONLY;
            
            sqlpp::sqlite3::connection source_db(source_config);
            
            // Get raw C handles
            sqlite3* pSrc = source_db.native_handle();
            sqlite3* pDest = m_db->native_handle();
            
            if (!pSrc || !pDest) {
                m_lastError = "Failed to get database handles";
                return false;
            }
            
            // Initialize backup
            sqlite3_backup* pBackup = sqlite3_backup_init(pDest, "main", pSrc, "main");
            if (!pBackup) {
                m_lastError = std::string("Backup init failed: ") + sqlite3_errmsg(pDest);
                return false;
            }
            
            // Incremental backup loop
            int rc;
            do {
                rc = sqlite3_backup_step(pBackup, pages_per_step);
                
                if (progress_callback) {
                    int remaining = sqlite3_backup_remaining(pBackup);
                    int total = sqlite3_backup_pagecount(pBackup);
                    progress_callback(remaining, total);
                }
                
                // Allow other work to happen (important for WASM)
                if (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                    sqlite3_sleep(10); // Give other threads/work a chance
                }
                
            } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);
            
            sqlite3_backup_finish(pBackup);
            
            if (rc != SQLITE_DONE) {
                m_lastError = std::string("Backup failed: ") + sqlite3_errmsg(pDest);
                return false;
            }
            
            return true;
            
        } catch (const std::exception& e) {
            m_lastError = std::string("Backup exception: ") + e.what();
            return false;
        }
    }

    /**
     * @brief Backup current database to a file
     * 
     * Useful for saving an in-memory database to disk/OPFS.
     * 
     * @param dest_path Path where to save the database
     * @return true if backup succeeded
     * 
     * Example:
     *   // Save memory database to OPFS
     *   DatabaseManager::Get().BackupToFile("/opfs/saved.db");
     */
    bool BackupToFile(const std::string& dest_path) {
        if (!m_db) {
            m_lastError = "Database not initialized";
            return false;
        }

        try {
            // Create destination database
            sqlpp::sqlite3::connection_config dest_config;
            dest_config.path_to_database = dest_path;
            dest_config.flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
            
            sqlpp::sqlite3::connection dest_db(dest_config);
            
            // Get raw C handles (note: reversed from BackupFromFile)
            sqlite3* pSrc = m_db->native_handle();
            sqlite3* pDest = dest_db.native_handle();
            
            if (!pSrc || !pDest) {
                m_lastError = "Failed to get database handles";
                return false;
            }
            
            // Perform backup
            sqlite3_backup* pBackup = sqlite3_backup_init(pDest, "main", pSrc, "main");
            if (!pBackup) {
                m_lastError = std::string("Backup init failed: ") + sqlite3_errmsg(pDest);
                return false;
            }
            
            int rc = sqlite3_backup_step(pBackup, -1);
            sqlite3_backup_finish(pBackup);
            
            if (rc != SQLITE_DONE) {
                m_lastError = std::string("Backup failed: ") + sqlite3_errmsg(pDest);
                return false;
            }
            
            return true;
            
        } catch (const std::exception& e) {
            m_lastError = std::string("Backup exception: ") + e.what();
            return false;
        }
    }

    /**
     * @brief Get raw sqlite3* handle for advanced operations
     * 
     * Provides access to the underlying C handle for operations not exposed
     * by sqlpp23, such as the SQLite Online Backup API, custom VFS, etc.
     * 
     * @return Raw sqlite3* pointer or nullptr if not initialized
     * 
     * Example:
     *   sqlite3* handle = DatabaseManager::Get().GetRawHandle();
     *   if (handle) {
     *       // Use raw SQLite C API
     *       sqlite3_exec(handle, "PRAGMA ...", nullptr, nullptr, nullptr);
     *   }
     */
    sqlite3* GetRawHandle() {
        return m_db ? m_db->native_handle() : nullptr;
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
