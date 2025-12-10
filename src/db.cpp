#include "db.hpp"
#include <iostream>

HistoryDB::HistoryDB(const std::string& db_path) : db_path_(db_path) {}

void HistoryDB::initSchema() {
    try {
        SQLite::Database db(db_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        
        // Table 1: Unique Command Strings
        db.exec("CREATE TABLE IF NOT EXISTS commands ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "cmd_text TEXT UNIQUE NOT NULL"
                ");");

        // Table 2: The Execution Timeline
        // Includes Git Branch and Success/Fail status
        db.exec("CREATE TABLE IF NOT EXISTS executions ("
                "id INTEGER PRIMARY KEY, "
                "command_id INTEGER, "
                "session_id TEXT, "
                "cwd TEXT, "
                "git_branch TEXT, " // Nullable active git branch
                "exit_code INTEGER, "
                "duration_ms INTEGER, "
                "timestamp INTEGER, "
                "FOREIGN KEY (command_id) REFERENCES commands (id)"
                ");");
    } catch (std::exception& e) {
        std::cerr << "DB Init Error: " << e.what() << std::endl;
    }
}

// Logic to insert command and link it to execution
void HistoryDB::logCommand(const std::string& cmd, const std::string& session, 
                           const std::string& cwd, const std::string& branch, 
                           int exit_code, int duration, long long timestamp) {
    try {
        SQLite::Database db(db_path_, SQLite::OPEN_READWRITE);
        
        // 1. Insert or Ignore the command string
        SQLite::Statement query(db, "INSERT OR IGNORE INTO commands (cmd_text) VALUES (?)");
        query.bind(1, cmd);
        query.exec();

        // 2. Get the Command ID
        SQLite::Statement idQuery(db, "SELECT id FROM commands WHERE cmd_text = ?");
        idQuery.bind(1, cmd);
        idQuery.executeStep();
        int cmd_id = idQuery.getColumn(0);

        // 3. Log execution
        SQLite::Statement logQuery(db, "INSERT INTO executions "
            "(command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)");
            
        logQuery.bind(1, cmd_id);
        logQuery.bind(2, session);
        logQuery.bind(3, cwd);
        
        if (branch.empty()) {
            logQuery.bind(4, (char*)nullptr); // Explicitly bind NULL
        } else {
            logQuery.bind(4, branch);
        }
        // ---------------------------------------------

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
                                            const std::string& context_val) {
    std::vector<SearchResult> results;
    try {
        SQLite::Database db(db_path_, SQLite::OPEN_READONLY);
        
        std::string sql = R"(
            SELECT DISTINCT c.cmd_text, MAX(e.timestamp) as last_used
            FROM executions e
            JOIN commands c ON e.command_id = c.id
            WHERE c.cmd_text LIKE ? 
        )";

        if (scope == SearchScope::DIRECTORY) {
            sql += " AND e.cwd = ? ";
        } else if (scope == SearchScope::BRANCH) {
            sql += " AND e.git_branch = ? ";
        }

        sql += " GROUP BY c.cmd_text ORDER BY last_used DESC LIMIT 10";

        SQLite::Statement q(db, sql);
        
        q.bind(1, "%" + query + "%");
        
        if (scope != SearchScope::GLOBAL) {
            q.bind(2, context_val);
        }

        while (q.executeStep()) {
            SearchResult res;
            res.cmd = q.getColumn(0).getString();
            results.push_back(res);
        }

    } catch (std::exception& e) {
        std::cerr << "Search Error: " << e.what() << std::endl;
    }
    return results;
}