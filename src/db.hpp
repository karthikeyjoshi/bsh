#pragma once
#include <SQLiteCpp/SQLiteCpp.h>
#include <string>
#include <vector>

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
};