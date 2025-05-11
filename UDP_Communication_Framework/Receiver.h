// The header file for Receiver class
// contains variables and methods declarations

#ifndef RECEIVER_H
#define RECEIVER_H

// These are required in the original framework
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include "ws2tcpip.h"

#include <string>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include "sha256.h"
#include "utils.h"
#include "CRC.h"



class Receiver {
public:
    // Constructor
    Receiver(int local_port, int target_port, wchar_t* target_IP);
    // Destructor
    ~Receiver();

    // Runs the main receiver program
    int run();

private:
    SOCKET socketS;
    struct sockaddr_in local;
    struct sockaddr_in from;
    struct sockaddr_in addrDest;
    int fromlen = sizeof(from);
    int retryCounter = 0;
    int lastSeqNum = -1;

    FILE* file_out = nullptr;
    char* fileName = nullptr;
    const char* received_sha256 = nullptr;
    SHA256 sha256;
    bool sha256_ok = false;
    bool crc_fail = false;

    // Private methods for handling specific packet types
    void handleDuplicatePacket(const Packet& packet);
    void handleFileSizePacket(const Packet& packet);
    void handleDataPacket(const Packet& packet);
    void handleFinalPacket(const Packet& packet);
};

#endif // RECEIVER_H