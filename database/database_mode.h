#pragma once

#include <string>

enum class DatabaseMode {
    Memory,       // :memory: - pure in-memory, no persistence
    NativeFile,   // disk-based SQLite file (native builds)
    OPFS          // Origin-Private FileSystem (WASM with persistence)
};

// Performance tuning presets
struct PerformanceTuning {
    bool enabled = true;               // Apply automatic tuning
    
    // SQLite PRAGMA settings
    std::string journal_mode = "WAL";  // WAL, DELETE, TRUNCATE, PERSIST, MEMORY, OFF
    std::string synchronous = "NORMAL"; // OFF, NORMAL, FULL, EXTRA
    int page_size = 4096;              // 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
    int cache_size_kb = 16384;         // Negative value = KB, positive = pages
    std::string temp_store = "MEMORY"; // DEFAULT, FILE, MEMORY
    
    // Mode-specific defaults
    static PerformanceTuning ForMemory() {
        return PerformanceTuning{
            .enabled = true,
            .journal_mode = "MEMORY",
            .synchronous = "OFF",
            .page_size = 4096,
            .cache_size_kb = 8192,
            .temp_store = "MEMORY"
        };
    }
    
    static PerformanceTuning ForNativeFile() {
        return PerformanceTuning{
            .enabled = true,
            .journal_mode = "WAL",
            .synchronous = "NORMAL",
            .page_size = 4096,
            .cache_size_kb = 8192,
            .temp_store = "MEMORY"
        };
    }
    
    static PerformanceTuning ForOPFS() {
        return PerformanceTuning{
            .enabled = true,
            .journal_mode = "WAL",
            .synchronous = "NORMAL",
            .page_size = 4096,
            .cache_size_kb = 16384,  // 16MB for OPFS (bigger cache = less I/O)
            .temp_store = "MEMORY"
        };
    }
    
    static PerformanceTuning Disabled() {
        return PerformanceTuning{.enabled = false};
    }
};

struct DatabaseConfig {
    DatabaseMode mode = DatabaseMode::Memory;
    std::string path;  // Empty for memory, file path for NativeFile/OPFS
    bool create_if_missing = true;
    PerformanceTuning tuning;  // Auto-initialized based on mode
    
    // Helper factory methods
    static DatabaseConfig Memory(PerformanceTuning tuning = PerformanceTuning::ForMemory()) {
        return DatabaseConfig{
            .mode = DatabaseMode::Memory,
            .tuning = tuning
        };
    }
    
    static DatabaseConfig NativeFile(const std::string& filepath, 
                                     PerformanceTuning tuning = PerformanceTuning::ForNativeFile()) {
        return DatabaseConfig{
            .mode = DatabaseMode::NativeFile, 
            .path = filepath,
            .tuning = tuning
        };
    }
    
    static DatabaseConfig OPFS(const std::string& filepath,
                               PerformanceTuning tuning = PerformanceTuning::ForOPFS()) {
        return DatabaseConfig{
            .mode = DatabaseMode::OPFS, 
            .path = filepath,
            .tuning = tuning
        };
    }
};
