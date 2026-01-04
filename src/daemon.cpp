#include "db.hpp"
#include "git_utils.hpp"
#include "ipc.hpp"
#include <string_view>
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
std::vector<std::string_view> split_msg(std::string_view msg) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true){
        size_t pos = msg.find(DELIMITER, start);
        if(pos == std::string_view::npos) {
            parts.emplace_back(msg.substr(start));
            break;
        }
        parts.emplace_back(msg.substr(start, pos - start));
        start = pos + 1;
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

    umask(0077);
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
    
    std::string socket_path = get_socket_path();
    unlink(socket_path.c_str());

    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0) exit(EXIT_FAILURE);

    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) exit(EXIT_FAILURE);
    chmod(socket_path.c_str(), 0600);
    if (listen(server_fd, 10) < 0) exit(EXIT_FAILURE);

    while (true) {
        int new_socket;
        if ((new_socket = accept(server_fd, nullptr, nullptr)) < 0) continue;

        char buffer[BUFFER_SIZE + 1];
        ssize_t bytes = read(new_socket, buffer, BUFFER_SIZE);
        if (bytes <= 0) {
            close(new_socket);
            continue;
        }
        buffer[bytes] = '\0';
        std::string_view request(buffer, bytes);
        auto args = split_msg(request);
        std::string response = "";

        if (args.empty()) {
            close(new_socket);
            continue;
        }

        try {
            std::string_view command = args[0];

            if (command == "SUGGEST" && args.size() >= 5) {
                std::string query (args[1]);
                std::string scope_str (args[2]);
                std::string ctx_val (args[3]);
                bool success = (args[4] == "1");

                SearchScope scope = SearchScope::GLOBAL;
                if (scope_str == "dir") scope = SearchScope::DIRECTORY;

                if (scope_str == "branch") {
                    auto branch_opt = get_git_branch(ctx_val);
                    if (branch_opt) {
                        scope = SearchScope::BRANCH;
                        ctx_val = *branch_opt;
                        response += "##BRANCH:" + ctx_val + "\n";
                    } 
                    else {
                        scope = SearchScope::DIRECTORY;
                        response += "##SCOPE:DIRECTORY\n";
                    }
                }

                auto results = history.search(query, scope, ctx_val, success);
                for (const auto& r : results) {
                    response += r.cmd + "\n";
                }
            }
            else if (command == "RECORD" && args.size() >= 6) {
                std::string cmd (args[1]);
                std::string sess (args[2]);
                std::string cwd (args[3]);
                int exit_code = args[4].empty() ? 0 : std::stoi(std::string(args[4]));
                int duration = args[5].empty() ? 0 : std::stoi(std::string(args[5]));
                
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