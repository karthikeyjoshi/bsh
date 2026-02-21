#pragma once
#include <SQLiteCpp/SQLiteCpp.h>
#include <string>
#include <vector>
#include <memory> 

enum class SearchScope { GLOBAL, DIRECTORY, BRANCH };

struct SearchResult {
    int id;
    std::string cmd;
};

class HistoryDB {
public:
    explicit HistoryDB(const std::string& db_path);
    void initSchema();
    
    void logCommand(const std::string& cmd, const std::string& session, 
                    const std::string& cwd, const std::string& branch, 
                    int exit_code, int duration, long long timestamp);

    std::vector<SearchResult> search(const std::string& query, 
                                     SearchScope scope,
                                     const std::string& context_val,
                                     bool only_success = false); 

private:
    std::string db_path_;
    
    std::unique_ptr<SQLite::Database> db_;
    std::unique_ptr<SQLite::Statement> stmt_insert_cmd_;
    std::unique_ptr<SQLite::Statement> stmt_get_id_;
    std::unique_ptr<SQLite::Statement> stmt_insert_exec_;
    std::unique_ptr<SQLite::Statement> stmt_upsert_ctx_;
    std::unique_ptr<SQLite::Statement> stmt_update_cmd_success_;
    std::unique_ptr<SQLite::Statement> stmt_search_global_;
    std::unique_ptr<SQLite::Statement> stmt_search_global_ok_;
    std::unique_ptr<SQLite::Statement> stmt_search_dir_;
    std::unique_ptr<SQLite::Statement> stmt_search_dir_ok_;
    std::unique_ptr<SQLite::Statement> stmt_search_branch_;
    std::unique_ptr<SQLite::Statement> stmt_search_branch_ok_;
};