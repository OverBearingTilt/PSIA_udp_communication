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

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <unordered_set>
#include <map>


#define BUFFERS_LEN 1024 - sizeof(uint32_t) - 2 * sizeof(int) - sizeof(char)
#define SHA256_LEN 64
#define TIMEOUT_MS 1500
#define WINDOW_SIZE 5

struct PacketEntry {
    Packet packet;
    bool acknowledged = false;
    std::chrono::steady_clock::time_point lastSent;
};


class Sender {
public:
    // Constructor - initializes socket and address
    Sender(int local_port, int target_port, wchar_t* target_IP);
    ~Sender();

    // Runs the main sender program
    int run(const std::string& filePath, const std::string& fileName);

private:
    // Socket-related members
    SOCKET socketS;
    sockaddr_in addrDest;
    sockaddr_in local;
    int seqNum;
    std::mutex ack_mutex;
    std::condition_variable ack_cv;
    PacketEntry buffer[WINDOW_SIZE];
    std::atomic<int> base;         // Oldest unacknowledged packet
    std::atomic<int> nextSeqNum;   // Next packet to send
    bool ackReceived[WINDOW_SIZE] = { false };
    std::unordered_set<int> earlyAcks;
    std::mutex earlyAckMutex;
    std::atomic<bool> stopThreads = false;

    void calculateSHA256(const std::string& filePath, std::string& hash);
    bool sendFileNamePacket(const std::string& fileName);
    bool sendDataPackets(const std::string& filePath, const std::string& sha256Hash);
    bool sendFinalPacket(const std::string& hash);
    bool handleACK(int expectedType, int expectedSeqNum);
    void sendPacket(Packet& packet);
    void waitForAcksThread();
};

#endif // SENDER_H