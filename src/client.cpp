#include "ipc.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 2) return 0;
    std::string mode = argv[1];
    
    std::string msg = "";

    if (mode == "suggest") {
        msg = "SUGGEST";
        msg += DELIMITER + std::string(argv[2]); // query
        
        std::string scope = "global";
        std::string context = "";
        std::string success = "0";

        // FIX: More robust loop
        for (int i=3; i<argc; i++) {
            std::string arg = argv[i];
            if (arg == "--scope" && i+1 < argc) {
                scope = argv[++i];
            }
            else if ((arg == "--cwd" || arg == "--branch") && i+1 < argc) {
                context = argv[++i];
            }
            else if (arg == "--success") {
                success = "1";
            }
        }
        msg += DELIMITER + scope + DELIMITER + context + DELIMITER + success;
    }
    else if (mode == "record") {
        msg = "RECORD";
        std::string cmd, session, cwd, exit_code, duration;
        
        for(int i=2; i<argc; i++) {
            std::string k = argv[i];
            if (i+1 < argc) {
                if(k == "--cmd") cmd = argv[++i];
                else if(k == "--session") session = argv[++i];
                else if(k == "--cwd") cwd = argv[++i];
                else if(k == "--exit") exit_code = argv[++i];
                else if(k == "--duration") duration = argv[++i];
            }
        }
        msg += DELIMITER + cmd + DELIMITER + session + DELIMITER + cwd + 
               DELIMITER + exit_code + DELIMITER + duration;
    }
    else {
        return 0; 
    }

    int sock = 0;
    struct sockaddr_un serv_addr;
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        return 1;
    }

    serv_addr.sun_family = AF_UNIX;
    std::string socket_path = get_socket_path();
    strncpy(serv_addr.sun_path, socket_path.c_str(), sizeof(serv_addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        return 1;
    }

    send(sock, msg.c_str(), msg.length(), 0);

    char buffer[BUFFER_SIZE] = {0};
    read(sock, buffer, BUFFER_SIZE);
    
    std::cout << buffer;
    
    close(sock);
    return 0;
}