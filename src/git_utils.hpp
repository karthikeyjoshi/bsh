#pragma once
#include <string>
#include <optional>

std::optional<std::string> get_git_branch(const std::string& cwd_path);
std::optional<std::string> get_git_branch_cached(const std::string& cwd_path);