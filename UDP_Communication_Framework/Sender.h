#ifndef SENDER_H
#define SENDER_H

#include <string>
#include "CRC.h"
#include "sha256.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include "utils.h"


#define BUFFERS_LEN 1024 - sizeof(uint32_t) - 2 * sizeof(int) - sizeof(char)
#define SHA256_LEN 64
#define TIMEOUT_SEC 1

class Sender {
public:
    Sender(int local_port, int target_port, wchar_t* target_IP);
    ~Sender();

    // Initializes the sender and sends the file
    int run(const std::string& filePath, const std::string& fileName);

private:
    void calculateSHA256(const std::string& filePath, std::string& hash);
    bool sendFileNamePacket(const std::string& fileName);
    bool sendDataPackets(const std::string& filePath);
    bool sendFinalPacket(const std::string& hash);
    bool waitForACK(int packetType, int seqNum);

    // Helper methods
    void resetDataBuffer(char* buffer, size_t size);
    bool isBufferAllNum(const char* buffer, int size, char setTo);

    // Socket-related members
    SOCKET socketS;
    sockaddr_in addrDest;
    sockaddr_in local;
};

#endif // SENDER_H
