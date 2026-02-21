#include "db.hpp"
#include <iostream>
#include <algorithm> 

std::string trim_cmd(const std::string& str) {
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::string sanitize_fts_query(std::string query) {
    std::replace(query.begin(), query.end(), '"', ' ');
    return "\"" + query + "\" *";
}

HistoryDB::HistoryDB(const std::string& db_path) : db_path_(db_path) {
    db_ = std::make_unique<SQLite::Database>(db_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db_->exec("PRAGMA journal_mode=WAL;");
    db_->exec("PRAGMA synchronous=NORMAL;");
    db_->exec("PRAGMA busy_timeout=5000;"); 
}

void HistoryDB::initSchema() {
    try {
        int current_version = db_->execAndGet("PRAGMA user_version").getInt();

        const int TARGET_VERSION = 4;

        bool needs_vacuum = false;

        while (current_version < TARGET_VERSION) {
            SQLite::Transaction transaction(*db_);
            
            if (current_version == 0) {
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
            }

            else if (current_version == 1) {
                db_->exec("CREATE VIRTUAL TABLE IF NOT EXISTS commands_fts USING fts5(cmd_text, content='commands', content_rowid='id');");

                db_->exec("CREATE TRIGGER IF NOT EXISTS commands_ai AFTER INSERT ON commands BEGIN "
                          "  INSERT INTO commands_fts(rowid, cmd_text) VALUES (new.id, new.cmd_text); "
                          "END;");

                db_->exec("INSERT INTO commands_fts(commands_fts) VALUES('rebuild');");

                current_version = 2;
                db_->exec("PRAGMA user_version = 2");
            }

            else if (current_version == 2) {
                db_->exec("ALTER TABLE commands ADD COLUMN last_timestamp INTEGER DEFAULT 0;");
                
                db_->exec("UPDATE commands SET last_timestamp = ("
                          "  SELECT MAX(timestamp) FROM executions "
                          "  WHERE executions.command_id = commands.id"
                          ");");
                
                db_->exec("CREATE INDEX IF NOT EXISTS idx_cmd_timestamp ON commands(last_timestamp);");

                needs_vacuum = true;

                current_version = 3;
                db_->exec("PRAGMA user_version = 3");
            }
            else if (current_version == 3) {
                db_->exec("DELETE FROM commands WHERE cmd_text LIKE 'bsh%' OR cmd_text LIKE './bsh%';");
                db_->exec("INSERT INTO commands_fts(commands_fts) VALUES('rebuild');");

                db_->exec("CREATE TABLE IF NOT EXISTS command_context ("
                          "command_id INTEGER, "
                          "cwd TEXT, "
                          "git_branch TEXT, "
                          "success_count INTEGER DEFAULT 0, "
                          "last_timestamp INTEGER, "
                          "PRIMARY KEY (command_id, cwd, git_branch)"
                          ");");
                          
                db_->exec("INSERT INTO command_context (command_id, cwd, git_branch, success_count, last_timestamp) "
                          "SELECT command_id, cwd, COALESCE(git_branch, ''), SUM(CASE WHEN exit_code = 0 THEN 1 ELSE 0 END), MAX(timestamp) "
                          "FROM executions GROUP BY command_id, cwd, COALESCE(git_branch, '')");
                
                db_->exec("CREATE INDEX IF NOT EXISTS idx_ctx_cwd ON command_context(cwd);");
                db_->exec("CREATE INDEX IF NOT EXISTS idx_ctx_branch ON command_context(git_branch);");

                db_->exec("ALTER TABLE commands ADD COLUMN success_count INTEGER DEFAULT 0;");
                db_->exec("UPDATE commands SET success_count = ("
                          "SELECT SUM(CASE WHEN exit_code = 0 THEN 1 ELSE 0 END) FROM executions WHERE executions.command_id = commands.id"
                          ");");

                needs_vacuum = true;
                current_version = 4;
                db_->exec("PRAGMA user_version = 4");
            }

            else {
                std::cerr << "NO Migration logic for v" << current_version << "->v" << (current_version+1) << std::endl;
                break;
            }

            transaction.commit();
        }
        if (needs_vacuum) {
            db_->exec("VACUUM;"); 
        }

        stmt_insert_cmd_ = std::make_unique<SQLite::Statement>(*db_, 
            "INSERT OR IGNORE INTO commands (cmd_text) VALUES (?)");
            
        stmt_get_id_ = std::make_unique<SQLite::Statement>(*db_, 
            "SELECT id FROM commands WHERE cmd_text = ?");
            
        stmt_insert_exec_ = std::make_unique<SQLite::Statement>(*db_, 
            "INSERT INTO executions (command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)");

        stmt_upsert_ctx_ = std::make_unique<SQLite::Statement>(*db_, 
            "INSERT INTO command_context (command_id, cwd, git_branch, success_count, last_timestamp) "
            "VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(command_id, cwd, git_branch) DO UPDATE SET "
            "success_count = success_count + excluded.success_count, "
            "last_timestamp = MAX(last_timestamp, excluded.last_timestamp)");

        stmt_update_cmd_success_ = std::make_unique<SQLite::Statement>(*db_, 
            "UPDATE commands SET last_timestamp = ?, success_count = success_count + ? WHERE id = ?");

        stmt_search_global_ = std::make_unique<SQLite::Statement>(*db_,
            "SELECT c.id, c.cmd_text FROM commands_fts fts "
            "JOIN commands c ON fts.rowid = c.id "
            "WHERE commands_fts MATCH ? ORDER BY c.last_timestamp DESC LIMIT 5");

        stmt_search_global_ok_ = std::make_unique<SQLite::Statement>(*db_,
            "SELECT c.id, c.cmd_text FROM commands_fts fts "
            "JOIN commands c ON fts.rowid = c.id "
            "WHERE commands_fts MATCH ? AND c.success_count > 0 ORDER BY c.last_timestamp DESC LIMIT 5");

        stmt_search_dir_ = std::make_unique<SQLite::Statement>(*db_,
            "SELECT c.id, c.cmd_text FROM commands_fts fts "
            "JOIN commands c ON fts.rowid = c.id "
            "JOIN command_context ctx ON ctx.command_id = c.id "
            "WHERE commands_fts MATCH ? AND ctx.cwd = ? "
            "GROUP BY c.id ORDER BY MAX(ctx.last_timestamp) DESC LIMIT 5");

        stmt_search_dir_ok_ = std::make_unique<SQLite::Statement>(*db_,
            "SELECT c.id, c.cmd_text FROM commands_fts fts "
            "JOIN commands c ON fts.rowid = c.id "
            "JOIN command_context ctx ON ctx.command_id = c.id "
            "WHERE commands_fts MATCH ? AND ctx.cwd = ? AND ctx.success_count > 0 "
            "GROUP BY c.id ORDER BY MAX(ctx.last_timestamp) DESC LIMIT 5");

        stmt_search_branch_ = std::make_unique<SQLite::Statement>(*db_,
            "SELECT c.id, c.cmd_text FROM commands_fts fts "
            "JOIN commands c ON fts.rowid = c.id "
            "JOIN command_context ctx ON ctx.command_id = c.id "
            "WHERE commands_fts MATCH ? AND ctx.git_branch = ? "
            "GROUP BY c.id ORDER BY MAX(ctx.last_timestamp) DESC LIMIT 5");

        stmt_search_branch_ok_ = std::make_unique<SQLite::Statement>(*db_,
            "SELECT c.id, c.cmd_text FROM commands_fts fts "
            "JOIN commands c ON fts.rowid = c.id "
            "JOIN command_context ctx ON ctx.command_id = c.id "
            "WHERE commands_fts MATCH ? AND ctx.git_branch = ? AND ctx.success_count > 0 "
            "GROUP BY c.id ORDER BY MAX(ctx.last_timestamp) DESC LIMIT 5");

    } catch (std::exception& e) {
        std::cerr << "DB Init Error: " << e.what() << std::endl;
    }
}

void HistoryDB::logCommand(const std::string& raw_cmd, const std::string& session, 
                           const std::string& cwd, const std::string& branch, 
                           int exit_code, int duration, long long timestamp) {
    
    std::string cmd = trim_cmd(raw_cmd);
    if (cmd.empty()) return; 

    if (cmd.starts_with("bsh ") || cmd == "bsh" || 
        cmd.starts_with("./bsh ") || cmd == "./bsh") {
        return;
    }

    try {
        stmt_insert_cmd_->reset();
        stmt_insert_cmd_->bind(1, cmd);
        stmt_insert_cmd_->exec();

        stmt_get_id_->reset();
        stmt_get_id_->bind(1, cmd);
        if (stmt_get_id_->executeStep()) {
            int cmd_id = stmt_get_id_->getColumn(0);
            std::string safe_branch = branch.empty() ? "" : branch;
            int is_success = (exit_code == 0) ? 1 : 0;

            stmt_insert_exec_->reset();
            stmt_insert_exec_->bind(1, cmd_id);
            stmt_insert_exec_->bind(2, session);
            stmt_insert_exec_->bind(3, cwd);
            stmt_insert_exec_->bind(4, safe_branch);
            stmt_insert_exec_->bind(5, exit_code);
            stmt_insert_exec_->bind(6, duration);
            stmt_insert_exec_->bind(7, (int64_t)timestamp);
            stmt_insert_exec_->exec();

            // 2. Upsert fast-path context table
            stmt_upsert_ctx_->reset();
            stmt_upsert_ctx_->bind(1, cmd_id);
            stmt_upsert_ctx_->bind(2, cwd);
            stmt_upsert_ctx_->bind(3, safe_branch);
            stmt_upsert_ctx_->bind(4, is_success);
            stmt_upsert_ctx_->bind(5, (int64_t)timestamp);
            stmt_upsert_ctx_->exec();

            // 3. Update fast-path global table
            stmt_update_cmd_success_->reset();
            stmt_update_cmd_success_->bind(1, (int64_t)timestamp);
            stmt_update_cmd_success_->bind(2, is_success);
            stmt_update_cmd_success_->bind(3, cmd_id);
            stmt_update_cmd_success_->exec();
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
        SQLite::Statement* stmt = nullptr;
        std::string fts_query = sanitize_fts_query(query);

        if (scope == SearchScope::GLOBAL) {
            stmt = only_success ? stmt_search_global_ok_.get() : stmt_search_global_.get();
            stmt->reset();
            stmt->bind(1, fts_query);
        }
        else if (scope == SearchScope::DIRECTORY) {
            stmt = only_success ? stmt_search_dir_ok_.get() : stmt_search_dir_.get();
            stmt->reset();
            stmt->bind(1, fts_query);
            stmt->bind(2, context_val);
        }
        else if (scope == SearchScope::BRANCH) {
            stmt = only_success ? stmt_search_branch_ok_.get() : stmt_search_branch_.get();
            std::string safe_branch = (context_val == "unknown") ? "" : context_val;
            stmt->reset();
            stmt->bind(1, fts_query);
            stmt->bind(2, safe_branch);
        }

        if (stmt) {
            while (stmt->executeStep()) {
                results.push_back({
                    stmt->getColumn(0),
                    stmt->getColumn(1)
                });
            }
        }
    } catch (std::exception& e) {
        std::cerr << "DB Search Error: " << e.what() << std::endl;
    }
    return results;
}