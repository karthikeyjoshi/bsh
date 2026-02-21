#include "git_utils.hpp"
#include <git2.h>
#include <iostream>
#include <unordered_map>
#include <chrono>
#include <mutex>

struct GitLib {
    GitLib() { git_libgit2_init(); }
    ~GitLib() { git_libgit2_shutdown(); }
};

std::optional<std::string> get_git_branch(const std::string& cwd_path) {
    static GitLib git_init;

    git_repository* repo = nullptr;
    git_reference* head = nullptr;
    std::string branch_name;
    bool found = false;

    int error = git_repository_open_ext(&repo, cwd_path.c_str(), 0, nullptr);
    
    if (error == 0) {
        error = git_repository_head(&head, repo);
        
        if (error == 0) {
            const char* name = git_reference_shorthand(head);
            if (name) {
                branch_name = name;
                found = true;
            }
        }
    }

    if (head) git_reference_free(head);
    if (repo) git_repository_free(repo);

    if (found) return branch_name;
    return std::nullopt;
}

struct CacheEntry {
    std::string branch;
    std::chrono::steady_clock::time_point timestamp;
};

static std::unordered_map<std::string, CacheEntry> branch_cache;
static std::mutex cache_mutex;

std::optional<std::string> get_git_branch_cached(const std::string& cwd_path) {
    auto now = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = branch_cache.find(cwd_path);
        if (it != branch_cache.end()) {
            if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count() < 2) {
                if (it->second.branch.empty()) return std::nullopt;
                return it->second.branch;
            }
        }
    }

    auto branch_opt = get_git_branch(cwd_path);

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        branch_cache[cwd_path] = {branch_opt.value_or(""), now};
    }
    
    return branch_opt;
}