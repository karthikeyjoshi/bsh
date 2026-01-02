#include "db.hpp"
#include <iostream>
#include <algorithm> 

// --- Helper: Trim String ---
std::string trim_cmd(const std::string& str) {
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

HistoryDB::HistoryDB(const std::string& db_path) : db_path_(db_path) {
    db_ = std::make_unique<SQLite::Database>(db_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db_->exec("PRAGMA journal_mode=WAL;");
    db_->exec("PRAGMA synchronous=NORMAL;");
}

void HistoryDB::initSchema() {
    try {
        // get current version of system
        int current_version = db_->execAndGet("PRAGMA user_version").getInt();

        // set target version of system
        const int TARGET_VERSION = 1;

        // migrating one step at a time
        while (current_version < TARGET_VERSION) {
            SQLite::Transaction transaction(*db_);
            
            if (current_version == 0) {
                // base schema
                db_->exec("CREATE TABLE IF NOT EXISTS commands ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "cmd_text TEXT UNIQUE NOT NULL"
                        ");");

                db_->exec("CREATE TABLE IF NOT EXISTS executions ("
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

                db_->exec("CREATE INDEX IF NOT EXISTS idx_exec_cwd ON executions(cwd);");
                db_->exec("CREATE INDEX IF NOT EXISTS idx_exec_branch ON executions(git_branch);");
                db_->exec("CREATE INDEX IF NOT EXISTS idx_exec_ts ON executions(timestamp);");

                current_version = 1;
                db_->exec("PRAGMA user_version = 1");
            } else {
                std::cerr << "NO Migration logic for v" << current_version << "->v" << (current_version+1) << std::endl;
                break;
            }

            transaction.commit();
        }

        stmt_insert_cmd_ = std::make_unique<SQLite::Statement>(*db_, 
            "INSERT OR IGNORE INTO commands (cmd_text) VALUES (?)");
            
        stmt_get_id_ = std::make_unique<SQLite::Statement>(*db_, 
            "SELECT id FROM commands WHERE cmd_text = ?");
            
        stmt_insert_exec_ = std::make_unique<SQLite::Statement>(*db_, 
            "INSERT INTO executions (command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)");

    } catch (std::exception& e) {
        std::cerr << "DB Init Error: " << e.what() << std::endl;
    }
}

void HistoryDB::logCommand(const std::string& raw_cmd, const std::string& session, 
                           const std::string& cwd, const std::string& branch, 
                           int exit_code, int duration, long long timestamp) {
    
    std::string cmd = trim_cmd(raw_cmd);
    if (cmd.empty()) return; 

    try {
        stmt_insert_cmd_->reset();
        stmt_insert_cmd_->bind(1, cmd);
        stmt_insert_cmd_->exec();

        stmt_get_id_->reset();
        stmt_get_id_->bind(1, cmd);
        if (stmt_get_id_->executeStep()) {
            int cmd_id = stmt_get_id_->getColumn(0);

            stmt_insert_exec_->reset();
            stmt_insert_exec_->bind(1, cmd_id);
            stmt_insert_exec_->bind(2, session);
            stmt_insert_exec_->bind(3, cwd);
            
            if (branch.empty()) stmt_insert_exec_->bind(4, (char*)nullptr);
            else stmt_insert_exec_->bind(4, branch);
            
            stmt_insert_exec_->bind(5, exit_code);
            stmt_insert_exec_->bind(6, duration);
            stmt_insert_exec_->bind(7, (int64_t)timestamp);
            stmt_insert_exec_->exec();
        }
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
        // FIX: GROUP BY TRIM(...) forces duplicates like "cd" and "cd " to merge.
        std::string sql = "SELECT MAX(c.id), TRIM(c.cmd_text) "
                          "FROM executions e "
                          "JOIN commands c ON e.command_id = c.id "
                          "WHERE TRIM(c.cmd_text) LIKE ? "
                          "AND c.cmd_text NOT LIKE 'bsh%' "
                          "AND c.cmd_text NOT LIKE './bsh%' "
                          "AND TRIM(c.cmd_text) NOT LIKE '#%' ";

        if (scope == SearchScope::DIRECTORY) {
            sql += " AND e.cwd = ?";
        }
        else if (scope == SearchScope::BRANCH) {
            if (context_val.empty() || context_val == "unknown") {
                sql += " AND (e.git_branch IS NULL OR e.git_branch = '')";
            } else {
                sql += " AND e.git_branch = ?";
            }
        }
        
        if (only_success) sql += " AND e.exit_code = 0";

        sql += " GROUP BY TRIM(c.cmd_text) ORDER BY MAX(e.timestamp) DESC LIMIT 5";

        SQLite::Statement query_stmt(*db_, sql);

        query_stmt.bind(1, "%" + query + "%");
        
        if (scope == SearchScope::DIRECTORY) {
            query_stmt.bind(2, context_val);
        }
        else if (scope == SearchScope::BRANCH) {
            if (!context_val.empty() && context_val != "unknown") {
                query_stmt.bind(2, context_val);
            }
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