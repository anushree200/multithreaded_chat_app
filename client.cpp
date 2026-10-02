// client.cpp
// Direct C++ port of client.java: prompts for a username, then runs
// two threads -- one reading stdin and sending, one reading the socket
// and printing -- exactly like the Java version's sendMessage/readMessage
// threads.

#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.hpp"

constexpr int SERVER_PORT = 1234;

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket failed"); return 1; }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed"); return 1;
    }

    std::cout << "Enter your username: ";
    std::string name;
    std::getline(std::cin, name);
    writeUTF(sock_fd, name); // Java: dos.writeUTF(name), sent once at the start

    // --- readMessage thread: blocks on readUTF, prints whatever arrives ---
    std::thread readMessage([sock_fd]() {
        while (true) {
            try {
                std::string msg = readUTF(sock_fd);
                std::cout << msg << "\n";
            } catch (const std::exception &e) {
                // Java's version catches and e.printStackTrace()s here
                // without breaking, which busy-loops forever once the
                // socket is dead. We break instead -- same spirit
                // (log and stop), without the infinite error spam.
                std::cerr << "[client] connection closed\n";
                break;
            }
        }
    });

    // --- sendMessage: main thread reads stdin lines, sends each one ---
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            writeUTF(sock_fd, line);
        } catch (const std::exception &e) {
            std::cerr << "[client] send failed: " << e.what() << "\n";
            break;
        }
    }

    readMessage.join();
    close(sock_fd);
    return 0;
}