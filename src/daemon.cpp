#include "db.hpp"
#include "git_utils.hpp"
#include "ipc.hpp"

#include <iostream>
#include <vector>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <csignal>

namespace fs = std::filesystem;

// --- Helper: Parsing ---
std::vector<std::string> split_msg(const std::string& msg) {
    std::vector<std::string> parts;
    std::stringstream ss(msg);
    std::string item;
    while (std::getline(ss, item, DELIMITER)) {
        parts.push_back(item);
    }
    return parts;
}

// --- Daemon Logic ---
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    chdir("/");
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

std::string get_db_path() {
    const char* home = std::getenv("HOME");
    fs::path dir = fs::path(home) / ".local" / "share" / "bsh";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return (dir / "history.db").string();
}

int main(int argc, char* argv[]) {
    daemonize();

    HistoryDB history(get_db_path());
    history.initSchema();

    int server_fd;
    struct sockaddr_un address;
    
    unlink(SOCKET_PATH.c_str());

    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0) exit(EXIT_FAILURE);

    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, SOCKET_PATH.c_str(), sizeof(address.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) exit(EXIT_FAILURE);
    if (listen(server_fd, 10) < 0) exit(EXIT_FAILURE);

    while (true) {
        int new_socket;
        if ((new_socket = accept(server_fd, nullptr, nullptr)) < 0) continue;

        char buffer[BUFFER_SIZE] = {0};
        read(new_socket, buffer, BUFFER_SIZE);
        
        std::string request(buffer);
        auto args = split_msg(request);
        std::string response = "";

        if (args.empty()) {
            close(new_socket);
            continue;
        }

        try {
            std::string command = args[0];

            // --- HANDLER: SUGGEST ---
            if (command == "SUGGEST" && args.size() >= 5) {
                std::string query = args[1];
                std::string scope_str = args[2];
                std::string ctx_val = args[3];
                bool success = (args[4] == "1");

                SearchScope scope = SearchScope::GLOBAL;
                if (scope_str == "dir") scope = SearchScope::DIRECTORY;
                
                if (scope_str == "branch") {
                    scope = SearchScope::BRANCH;
                    
                    // --- FIX START ---
                    // The Client (via Zsh) has ALREADY resolved the branch name.
                    // ctx_val contains "main", "feature/login", or "unknown".
                    
                    // We DO NOT call get_git_branch(ctx_val) here, because ctx_val 
                    // is a name, not a directory path.
                    
                    // Normalize "unknown" to empty string for the DB query
                    if (ctx_val == "unknown") {
                        ctx_val = "";
                    }
                    // --- FIX END ---
                }

                auto results = history.search(query, scope, ctx_val, success);
                for (const auto& r : results) {
                    response += r.cmd + "\n";
                }
            }

            // --- HANDLER: RECORD ---
            else if (command == "RECORD" && args.size() >= 6) {
                std::string cmd = args[1];
                std::string sess = args[2];
                std::string cwd = args[3];
                int exit_code = (args[4].empty()) ? 0 : std::stoi(args[4]);
                int duration = (args[5].empty()) ? 0 : std::stoi(args[5]);
                
                // For RECORD, we DO need to resolve the branch from the CWD
                std::string branch = "";
                auto branch_opt = get_git_branch(cwd);
                if (branch_opt) branch = *branch_opt;

                history.logCommand(cmd, sess, cwd, branch, exit_code, duration, time(nullptr));
                response = "OK";
            }
        } catch (const std::exception& e) {
            response = "ERR";
        }

        send(new_socket, response.c_str(), response.size(), 0);
        close(new_socket);
    }
    return 0;
}