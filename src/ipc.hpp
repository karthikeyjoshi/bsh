#pragma once
#include <string>
#include <unistd.h>
#include <cstdlib>

inline std::string get_socket_path() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir) {
        return std::string(runtime_dir) + "/bsh.sock";
    }
    return "/tmp/bsh_" + std::to_string(getuid()) + ".sock";
}
// Special delimiter that won't appear in normal shell commands
const char DELIMITER = '\x1F'; 
const int BUFFER_SIZE = 8192;