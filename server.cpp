// server.cpp
// Direct C++ port of the Java chat server (ServerSocket + one thread per
// client). Wire protocol matches Java's DataInputStream/DataOutputStream
// readUTF/writeUTF (see protocol.hpp), so the logic -- not just the
// language -- mirrors the original line by line.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.hpp"

constexpr int SERVER_PORT = 1234;

class ClientHandler; // forward declaration

// ---------------------------------------------------------
// Global state (mirrors the Java `static` fields on `server`
// and `ClientHandler`)
// ---------------------------------------------------------
std::vector<ClientHandler *> client_array;   // Java: Vector<ClientHandler> array
std::mutex array_mutex;
std::atomic<int> client_counter{0};          // Java: static int i

// Java: ConcurrentHashMap<String, Map<String, Set<ClientHandler>>> rooms
// The original wraps each room's client set inside an inner map keyed
// "clients" -- that inner map is never used for anything else, so here
// it is flattened to room_name -> set<ClientHandler*>. Behaviorally
// identical; just fewer indirections.
std::map<std::string, std::set<ClientHandler *>> rooms;
std::mutex rooms_mutex;

class ClientHandler {
public:
    int fd;
    std::string name;
    std::atomic<bool> isloggedin{true};

    std::set<std::string> joinedRooms;   // Java: Set<String> joinedRooms
    std::string currentRoom;             // "" means null
    std::mutex state_mutex;              // guards joinedRooms/currentRoom

    ClientHandler(int fd_, std::string name_) : fd(fd_), name(std::move(name_)) {}

    void run() {
        // Java: try (FileWriter writer = new FileWriter("chat_log.txt", true))
        // opened once for the lifetime of this client's thread, appending.
        std::ofstream log("chat_log.txt", std::ios::app);

        while (isloggedin) {
            std::string received;
            try {
                received = readUTF(fd);
            } catch (const std::exception &e) {
                break; // client disconnected / socket error
            }

            log << name << " to recipient: " << received << "\n" << std::flush;
            std::cout << received << "\n";

            if (received == "logout") {
                isloggedin = false;
                close(fd);
                break;
            }

            processMessage(received);
        }
    }

private:
    void processMessage(const std::string &received) {
        try {
            if (received.rfind("/broadcast", 0) == 0) {
                // Java: received.substring(10)  -- "/broadcast" is 10 chars
                std::string msg = received.substr(10);
                broadcast(msg);
                return;
            }

            if (received == "list") {
                std::string userList = "Users:\n";
                std::lock_guard<std::mutex> lock(array_mutex);
                for (auto *mc : client_array) {
                    if (mc->isloggedin) userList += mc->name + "\n";
                }
                writeUTF(fd, userList);
                return;
            }

            if (!received.empty() && received[0] == '@') {
                // Java: split(" ", 2) -> [targetUser, message]
                size_t sp = received.find(' ');
                if (sp != std::string::npos) {
                    std::string targetUser = received.substr(1, sp - 1);
                    std::string message = received.substr(sp + 1);

                    std::lock_guard<std::mutex> lock(array_mutex);
                    for (auto *ch : client_array) {
                        if (ch->name == targetUser && ch->isloggedin) {
                            writeUTF(ch->fd, "[Private] " + name + ": " + message);
                        }
                    }
                }
                return;
            }

            if (received == "r/create") {
                static std::mt19937 rng(std::random_device{}());
                std::uniform_int_distribution<int> dist(0, 9999);
                std::string roomname = "room_" + std::to_string(dist(rng));

                {
                    std::lock_guard<std::mutex> lock(rooms_mutex);
                    rooms[roomname]; // creates empty set, mirrors rooms.put(roomname, ...)
                }
                std::cout << "Room created: " << roomname << "\n";
                writeUTF(fd, "Room created: " + roomname);
                return;
            }

            if (received.rfind("r/join ", 0) == 0) {
                std::string roomasked = received.substr(7);

                std::lock_guard<std::mutex> state_lock(state_mutex);
                bool already = joinedRooms.count(roomasked) > 0;

                if (!already) {
                    {
                        std::lock_guard<std::mutex> lock(rooms_mutex);
                        rooms[roomasked].insert(this); // computeIfAbsent + add
                    }
                    joinedRooms.insert(roomasked);
                    if (currentRoom.empty()) currentRoom = roomasked;

                    std::cout << name << " joined " << roomasked << "\n";
                    writeUTF(fd, "Joined room: " + roomasked);
                } else {
                    writeUTF(fd, "You are already in room: " + roomasked);
                }
                return;
            }

            if (received == "r/list") {
                std::lock_guard<std::mutex> lock(state_mutex);
                std::string roomList = "Your joined rooms:\n";
                if (joinedRooms.empty()) {
                    roomList += "No rooms joined.";
                } else {
                    for (const auto &room : joinedRooms) {
                        roomList += room;
                        if (room == currentRoom) roomList += " (current)";
                        roomList += "\n";
                    }
                }
                writeUTF(fd, roomList);
                return;
            }

            if (received == "r/whothere") {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (currentRoom.empty()) {
                    writeUTF(fd, "No current room selected. Use r/switch or r/join first.");
                    return;
                }
                std::string clientList = "Who are there in this room (" + currentRoom + "):\n";
                std::lock_guard<std::mutex> rlock(rooms_mutex);
                auto it = rooms.find(currentRoom);
                if (it == rooms.end() || it->second.empty()) {
                    clientList += "No one is in this room.";
                } else {
                    for (auto *client : it->second) {
                        if (client->isloggedin) clientList += client->name + "\n";
                    }
                }
                writeUTF(fd, clientList);
                return;
            }

            if (received.rfind("r/switch ", 0) == 0) {
                std::string roomName = received.substr(9);
                std::lock_guard<std::mutex> lock(state_mutex);
                if (joinedRooms.count(roomName)) {
                    currentRoom = roomName;
                    writeUTF(fd, "Switched to room: " + roomName);
                } else {
                    writeUTF(fd, "You are not in room: " + roomName);
                }
                return;
            }

            if (received.rfind("r/leave ", 0) == 0) {
                std::string roomName = received.substr(8);
                std::lock_guard<std::mutex> lock(state_mutex);
                if (joinedRooms.erase(roomName)) {
                    {
                        std::lock_guard<std::mutex> rlock(rooms_mutex);
                        auto it = rooms.find(roomName);
                        if (it != rooms.end()) it->second.erase(this);
                    }
                    if (roomName == currentRoom) {
                        currentRoom = joinedRooms.empty() ? "" : *joinedRooms.begin();
                    }
                    std::cout << name << " left " << roomName << "\n";
                    writeUTF(fd, "Left room: " + roomName);
                    if (!currentRoom.empty()) {
                        writeUTF(fd, "Current room set to: " + currentRoom);
                    }
                } else {
                    writeUTF(fd, "You are not in room: " + roomName);
                }
                return;
            }

            if (received.rfind("r/send ", 0) == 0) {
                std::string currentRoomCopy;
                {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    currentRoomCopy = currentRoom;
                }
                if (currentRoomCopy.empty()) {
                    writeUTF(fd, "No current room selected. Use r/switch or r/join first.");
                    return;
                }
                std::string message = received.substr(7);
                sendToRoom(currentRoomCopy, message);
                return;
            }

            if (received.rfind("/file", 0) == 0) {
                // Java: StringTokenizer(received, "#") -> token0="/file", token1=filename
                size_t hash = received.find('#');
                std::string fileName = (hash != std::string::npos) ? received.substr(hash + 1) : "";

                // NOTE: reads raw bytes directly off the socket (not via
                // readUTF), matching the original -- the companion
                // client-side "send raw file bytes" code was never shown
                // in client.java either, so this half of the feature is
                // inherited as-is from the original design.
                std::ofstream fos(fileName, std::ios::binary);
                char buffer[4096];
                while (true) {
                    ssize_t bytesRead = recv(fd, buffer, sizeof(buffer), 0);
                    if (bytesRead <= 0) break;
                    fos.write(buffer, bytesRead);
                    if (bytesRead < (ssize_t)sizeof(buffer)) break;
                }
                writeUTF(fd, "File received: " + fileName);
                return;
            }

            if (received == "history") {
                int N = 100;
                std::ifstream in("chat_log.txt");
                if (!in) {
                    std::cout << "File not found!\n";
                    return;
                }
                std::string line;
                while (N > 0 && std::getline(in, line)) {
                    writeUTF(fd, line);
                    N--;
                }
                return;
            }
        } catch (const std::exception &e) {
            std::cerr << "processMessage error: " << e.what() << "\n";
        }
    }

    void sendToRoom(const std::string &roomName, const std::string &message) {
        std::lock_guard<std::mutex> lock(rooms_mutex);
        auto it = rooms.find(roomName);
        if (it == rooms.end()) return;
        for (auto *client : it->second) {
            if (client != this && client->isloggedin) {
                try {
                    writeUTF(client->fd, "[" + roomName + "] " + name + ": " + message);
                } catch (const std::exception &) {
                    // mirrors Java's catch-and-log-only behavior
                }
            }
        }
    }

    void broadcast(const std::string &message) {
        std::lock_guard<std::mutex> lock(array_mutex);
        for (auto *client : client_array) {
            if (client != this && client->isloggedin) {
                try {
                    writeUTF(client->fd, "Broadcast from " + name + ": " + message);
                } catch (const std::exception &) {
                }
            }
        }
    }
};

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket failed"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); close(server_fd); return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen failed"); close(server_fd); return 1;
    }

    std::cout << "Server listening on port " << SERVER_PORT << "...\n" << std::flush;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept failed"); continue; }

        std::cout << "New client request received, fd=" << client_fd << "\n" << std::flush;
        std::cout << "Creating a new handler for this client...\n" << std::flush;

        std::string name;
        try {
            name = readUTF(client_fd); // Java: dis.readUTF() for the username, sent first
        } catch (const std::exception &e) {
            close(client_fd);
            continue;
        }

        auto *handler = new ClientHandler(client_fd, name);

        std::cout << "Adding this client to active clients\n" << std::flush;
        {
            std::lock_guard<std::mutex> lock(array_mutex);
            client_array.push_back(handler);
        }

        std::thread(&ClientHandler::run, handler).detach();
        client_counter++;
    }

    close(server_fd);
    return 0;
}