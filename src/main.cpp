#include "db.hpp"
#include "git_utils.hpp"
#include "tui.hpp"
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <map>

namespace fs = std::filesystem;

// Helper: Resolve ~/.local/share/bsh/history.db
std::string get_db_path() {
    const char* home = std::getenv("HOME");
    if (!home) return "history.db"; // Fallback
    
    fs::path dir = fs::path(home) / ".local" / "share" / "bsh";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    return (dir / "history.db").string();
}

int main(int argc, char* argv[]) {
    // 1. Setup Database
    std::string dbFile = get_db_path();
    HistoryDB history(dbFile);
    history.initSchema();

    // 2. Parse Arguments (Basic Manual Parser for MVP)
    // Usage: bsh record --cmd "ls" --cwd "/tmp" --exit 0 --duration 100
    if (argc < 2) return 0;

    std::string mode = argv[1];

    if (mode == "record") {
        std::map<std::string, std::string> args;
        for (int i = 2; i < argc - 1; i += 2) {
            args[argv[i]] = argv[i+1];
        }

        std::string cmd = args["--cmd"];
        std::string cwd = args["--cwd"];
        std::string exit_str = args["--exit"];
        std::string duration_str = args["--duration"];
        std::string session = args["--session"];
        
        if (cmd.empty()) return 1; // Nothing to record

        // 3. Resolve Git Branch
        // If cwd is not provided, use current process path
        if (cwd.empty()) cwd = fs::current_path().string();
        
        std::string branch = ""; // Default if not in repo
        auto detected_branch = get_git_branch(cwd);
        if (detected_branch) {
            branch = *detected_branch;
        }

        // 4. Log to DB
        int exit_code = exit_str.empty() ? 0 : std::stoi(exit_str);
        int duration = duration_str.empty() ? 0 : std::stoi(duration_str);

        long long now = (long long)std::time(nullptr);
        history.logCommand(cmd, session, cwd, branch, exit_code, duration, now);
    }

    else if (mode == "search") {
        std::string selection = run_search_ui(history);
        
        // Print selected command to stdout so the shell can capture it
        if (!selection.empty()) {
            std::cout << selection; 
        }
    }

    else if (mode == "suggest") {
        // Usage: bsh suggest "query" --scope [global|dir|branch] --cwd "..." --branch "..." --success
        if (argc < 3) return 0;
        
        std::string query = argv[2];
        SearchScope scope = SearchScope::GLOBAL;
        std::string context_val = "";
        bool success_only = false; 
        
        // Loop starts at 3 to skip [program, mode, query]
        for (int i = 3; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "--scope" && i + 1 < argc) {
                std::string s = argv[++i]; // Increment i to consume value
                if (s == "dir") scope = SearchScope::DIRECTORY;
                if (s == "branch") scope = SearchScope::BRANCH;
            }
            else if (arg == "--cwd" && i + 1 < argc) {
                // Only capture if scope expects it, or capture anyway to be safe
                if (scope == SearchScope::DIRECTORY) context_val = argv[++i];
                else i++; // Skip value if not relevant
            }
            else if (arg == "--branch" && i + 1 < argc) {
                if (scope == SearchScope::BRANCH) context_val = argv[++i];
                else i++;
            }
            else if (arg == "--success") { 
                success_only = true; // <--- 2. Capture flag
            }
        }

        // <--- 3. Pass success_only to search
        auto results = history.search(query, scope, context_val, success_only);
        
        for (const auto& r : results) {
            std::cout << r.cmd << "\n";
        }
    }
    return 0;
}