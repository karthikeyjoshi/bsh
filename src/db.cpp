#include "db.hpp"
#include <iostream>

HistoryDB::HistoryDB(const std::string& db_path) : db_path_(db_path) {}

void HistoryDB::initSchema() {
    try {
        SQLite::Database db(db_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        
        // --- CRITICAL PERFORMANCE FIXES ---
        // Enable Write-Ahead Logging to allow concurrent read/write (prevents "database locked")
        db.exec("PRAGMA journal_mode=WAL;");
        db.exec("PRAGMA synchronous=NORMAL;");

        // Table 1: Unique Command Strings
        db.exec("CREATE TABLE IF NOT EXISTS commands ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "cmd_text TEXT UNIQUE NOT NULL"
                ");");

        // Table 2: The Execution Timeline
        db.exec("CREATE TABLE IF NOT EXISTS executions ("
                "id INTEGER PRIMARY KEY, "
                "command_id INTEGER, "
                "session_id TEXT, "
                "cwd TEXT, "
                "git_branch TEXT, "
                "exit_code INTEGER, "
                "duration_ms INTEGER, "
                "timestamp INTEGER, "
                "FOREIGN KEY (command_id) REFERENCES commands (id)"
                ");");

        // --- INDEXES (Missing in original) ---
        // Essential for O(1) lookups instead of full table scans
        db.exec("CREATE INDEX IF NOT EXISTS idx_exec_cwd ON executions(cwd);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_exec_branch ON executions(git_branch);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_exec_exit ON executions(exit_code);");
        db.exec("CREATE INDEX IF NOT EXISTS idx_exec_ts ON executions(timestamp);");

    } catch (std::exception& e) {
        std::cerr << "DB Init Error: " << e.what() << std::endl;
    }
}

void HistoryDB::logCommand(const std::string& cmd, const std::string& session, 
                           const std::string& cwd, const std::string& branch, 
                           int exit_code, int duration, long long timestamp) {
    try {
        SQLite::Database db(db_path_, SQLite::OPEN_READWRITE);
        
        // 1. Insert or Ignore Command
        SQLite::Statement query(db, "INSERT OR IGNORE INTO commands (cmd_text) VALUES (?)");
        query.bind(1, cmd);
        query.exec();

        // 2. Get Command ID
        SQLite::Statement idQuery(db, "SELECT id FROM commands WHERE cmd_text = ?");
        idQuery.bind(1, cmd);
        idQuery.executeStep();
        int cmd_id = idQuery.getColumn(0);

        // 3. Log Execution
        SQLite::Statement logQuery(db, "INSERT INTO executions "
            "(command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");
            
        logQuery.bind(1, cmd_id);
        logQuery.bind(2, session);
        logQuery.bind(3, cwd);
        if (branch.empty()) logQuery.bind(4, (char*)nullptr);
        else logQuery.bind(4, branch);
        logQuery.bind(5, exit_code);
        logQuery.bind(6, duration);
        logQuery.bind(7, timestamp);
        logQuery.exec();

    } catch (std::exception& e) {
        std::cerr << "Log Error: " << e.what() << std::endl;
    }
}

std::vector<SearchResult> HistoryDB::search(const std::string& query, 
                                            SearchScope scope,
                                            const std::string& context_val,
                                            bool only_success) {
    std::vector<SearchResult> results;
    try {
        SQLite::Database db(db_path_, SQLite::OPEN_READONLY);
        
        // 1. Build Query with JOIN
        // We join 'executions' (e) with 'commands' (c) to get the text + context
        std::string sql = "SELECT c.id, c.cmd_text "
                          "FROM executions e "
                          "JOIN commands c ON e.command_id = c.id "
                          "WHERE c.cmd_text LIKE ? "
                          "AND c.cmd_text NOT LIKE 'bsh%' "   
                          "AND c.cmd_text NOT LIKE './bsh%' "
                          "AND TRIM(c.cmd_text) NOT LIKE '#%' ";
        
        // 2. Add Scope Filters
        if (scope == SearchScope::DIRECTORY) {
            sql += " AND e.cwd = ?";
        } else if (scope == SearchScope::BRANCH) {
            sql += " AND e.git_branch = ?"; // Note: Schema uses 'git_branch', not 'branch'
        }

        // 3. Add Success Filter
        if (only_success) {
            sql += " AND e.exit_code = 0";
        }

        // 4. Group and Order
        // GROUP BY: Ensures we don't see the same command 50 times
        // MAX(timestamp): Shows the most recent usage of that command
        sql += " GROUP BY c.cmd_text ORDER BY MAX(e.timestamp) DESC LIMIT 5";

        SQLite::Statement query_stmt(db, sql);

        // 5. Bind Parameters
        query_stmt.bind(1, "%" + query + "%");

        if (scope != SearchScope::GLOBAL) {
            query_stmt.bind(2, context_val);
        }

        while (query_stmt.executeStep()) {
            results.push_back({
                query_stmt.getColumn(0),
                query_stmt.getColumn(1)
            });
        }
    } catch (std::exception& e) {
        std::cerr << "DB Error: " << e.what() << std::endl;
    }
    return results;
}