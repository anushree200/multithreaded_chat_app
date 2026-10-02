// protocol.hpp
//
// Replicates Java's DataOutputStream.writeUTF() / DataInputStream.readUTF():
// a 2-byte big-endian length prefix, followed by that many UTF-8 bytes.
// This is NOT a standard C++ idiom -- it exists purely so our C++ client
// and server speak the exact same wire format the Java version used.

#pragma once
#include <string>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

// Send all bytes, looping in case send() writes fewer than requested
// (send() is allowed to do partial writes on a TCP socket).
inline void send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) throw std::runtime_error("send failed / connection closed");
        sent += n;
    }
}

// Receive exactly len bytes (recv() can also return partial data).
inline void recv_all(int fd, char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, buf + got, len - got, 0);
        if (n <= 0) throw std::runtime_error("recv failed / connection closed");
        got += n;
    }
}

// Equivalent of DataOutputStream.writeUTF(String)
inline void writeUTF(int fd, const std::string &s) {
    uint16_t len = static_cast<uint16_t>(s.size());
    uint16_t len_be = htons(len);
    send_all(fd, reinterpret_cast<char *>(&len_be), 2);
    if (len > 0) send_all(fd, s.data(), len);
}

// Equivalent of DataInputStream.readUTF()
inline std::string readUTF(int fd) {
    uint16_t len_be;
    recv_all(fd, reinterpret_cast<char *>(&len_be), 2);
    uint16_t len = ntohs(len_be);
    std::string s(len, '\0');
    if (len > 0) recv_all(fd, s.data(), len);
    return s;
}