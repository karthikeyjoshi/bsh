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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace fs = std::filesystem;

size_t utf8_length(const std::string& str) {
    size_t len = 0;
    for (char c : str) {
        if ((c & 0xC0) != 0x80) len++;
    }
    return len;
}

std::string truncate_utf8(const std::string& str, size_t max_chars) {
    std::string res;
    size_t chars = 0;
    for (char c : str) {
        res += c;
        if ((c & 0xC0) != 0x80) chars++;
        if (chars >= max_chars) break;
    }
    return res;
}

std::string pad_right(const std::string& str, size_t target_len, const std::string& pad_char = " ") {
    size_t current = utf8_length(str);
    if (current >= target_len) return str;
    std::string res = str;
    for (size_t i = 0; i < target_len - current; ++i) {
        res += pad_char;
    }
    return res;
}

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

struct RecordTask {
    std::string cmd;
    std::string session;
    std::string cwd;
    std::string branch;
    int exit_code;
    int duration;
    long long timestamp;
};

std::queue<RecordTask> record_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;

void writer_thread_loop(const std::string& db_path) {
    HistoryDB history_writer(db_path);
    
    history_writer.initSchema(); 

    while (true) {
        RecordTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, []{ return !record_queue.empty(); });
            task = record_queue.front();
            record_queue.pop();
        }
        history_writer.logCommand(task.cmd, task.session, task.cwd, task.branch, task.exit_code, task.duration, task.timestamp);
    }
}

int main(int argc, char* argv[]) {
    daemonize();

    HistoryDB history(get_db_path());
    history.initSchema();

    std::thread writer_thread(writer_thread_loop, get_db_path());
    writer_thread.detach();

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
                int term_width = 80;
                if (args.size() >= 6 && !args[5].empty()) {
                    try { term_width = std::stoi(std::string(args[5])); } catch(...) {}
                }

                SearchScope scope = SearchScope::GLOBAL;
                std::string header_text = " BSH: Global ";
                
                if (scope_str == "dir") {
                    scope = SearchScope::DIRECTORY;
                    header_text = " BSH: Directory ";
                }
                else if (scope_str == "branch") {
                    auto branch_opt = get_git_branch_cached(ctx_val);
                    if (branch_opt && !branch_opt->empty() && *branch_opt != "unknown") {
                        scope = SearchScope::BRANCH;
                        ctx_val = *branch_opt;
                        header_text = " BSH: Branch (" + ctx_val + ") ";
                    } else {
                        response = "##SKIP##\n";
                        send(new_socket, response.c_str(), response.size(), 0);
                        close(new_socket);
                        continue;
                    }
                }

                if (success) {
                    header_text.pop_back(); 
                    header_text += " [OK] ";
                }

                auto results = history.search(query, scope, ctx_val, success);

                if (results.empty()) {
                    close(new_socket);
                    continue; 
                }

                for (const auto& r : results) {
                    response += r.cmd + "\n";
                }
                response += "##BOX##\n";

                std::vector<std::string> display_lines;
                size_t max_len = utf8_length(header_text);
                int safe_limit = term_width - 7;
                if (safe_limit < 10) safe_limit = 10;

                for (size_t i = 0; i < results.size(); ++i) {
                    std::string line = std::to_string(i + 1) + ": " + results[i].cmd;
                    if (utf8_length(line) > (size_t)safe_limit) {
                        line = truncate_utf8(line, safe_limit - 3) + "...";
                    }
                    line = " " + line; 
                    size_t len = utf8_length(line);
                    if (len > max_len) max_len = len;
                    display_lines.push_back(line);
                }

                max_len += 4; 

                std::string top_border = "\n" + pad_right("╭" + header_text, max_len + 1, "─") + "╮\n";
                response += top_border;

                for (const auto& dl : display_lines) {
                    response += "│" + pad_right(dl, max_len, " ") + "│\n";
                }

                std::string bottom_border = pad_right("╰", max_len + 1, "─") + "╯\n";
                response += bottom_border;
            }

            else if (command == "RECORD" && args.size() >= 6) {
                std::string cmd (args[1]);
                std::string sess (args[2]);
                std::string cwd (args[3]);
                int exit_code = args[4].empty() ? 0 : std::stoi(std::string(args[4]));
                int duration = args[5].empty() ? 0 : std::stoi(std::string(args[5]));
                
                std::string branch = "";
                auto branch_opt = get_git_branch_cached(cwd);
                if (branch_opt) branch = *branch_opt;

                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    record_queue.push({cmd, sess, cwd, branch, exit_code, duration, (long long)time(nullptr)});
                }
                queue_cv.notify_one();
                
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