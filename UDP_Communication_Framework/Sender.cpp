#include "stdafx.h"
#include "Sender.h"
#define ERROR

#pragma comment(lib, "ws2_32.lib")

Packet packetError(Packet packet) {
#ifdef ERROR
    if ((packet.seqNum + 1) % 10 == 0) {
        Packet pck;
        switch (packet.type) {
        case FILESIZE:
            pck.type = FILESIZE;
            pck.seqNum = packet.seqNum;
            pck.dataSize = packet.dataSize;
            std::strcpy(pck.fileName, packet.fileName);
            pck.crc = packet.crc == 0 ? 1 : 0;
            return pck;
        case DATA:
            pck.type = DATA;
            pck.seqNum = packet.seqNum;
            pck.dataSize = packet.dataSize;
            std::memcpy(pck.data, packet.data, packet.dataSize);
            pck.crc = packet.crc == 0 ? 1 : 0;
            return pck;
        case FINAL:
            pck.type = FINAL;
            pck.seqNum = packet.seqNum;
            pck.dataSize = packet.dataSize;
            std::memcpy(pck.hashArray, packet.hashArray, packet.dataSize);
            pck.crc = packet.crc == 0 ? 1 : 0;
            return pck;
        default:
            // Handle unexpected packet types
            return packet;
        }
    }
#endif
    return packet;
}

Sender::Sender(int local_port, int target_port, wchar_t* target_IP) : base(0), nextSeqNum(0) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Initialize socket
    socketS = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketS == INVALID_SOCKET) {
        throw std::runtime_error("Socket creation failed");
    }

    // Configure local address
    local.sin_family = AF_INET;
    local.sin_port = htons(local_port);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0) {
        throw std::runtime_error("Binding error");
    }

    // Configure destination address
    addrDest.sin_family = AF_INET;
    addrDest.sin_port = htons(target_port);
    InetPton(AF_INET, target_IP, &addrDest.sin_addr.s_addr);
}

Sender::~Sender() {
    closesocket(socketS);
    WSACleanup();
}



void Sender::sendPacket(Packet& packet) {
    sendto(socketS, (char*)&packet, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
}

int Sender::run(const std::string& filePath, const std::string& fileName) {
    try {
        std::string sha256Hash;
        calculateSHA256(filePath, sha256Hash);
        char try_counter = 0;
        bool sha_ok = false;

        while (try_counter < 3 && !sha_ok) {
            if (!sendFileNamePacket(fileName)) {
                ++try_counter;
                continue;
            }

            buffer[0].acknowledged = true; // Mark the first packet as acknowledged
            ackReceived[0] = true;

            if (sendDataPackets(filePath, sha256Hash)) {
                sha_ok = true;
                return 0;
            }
            else {
                ++try_counter;
                continue;
            }
        }

        return sha_ok ? 0 : 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

void Sender::calculateSHA256(const std::string& filePath, std::string& hash) {
    SHA256 sha256;
    std::ifstream file(filePath, std::ios::binary);
    const std::size_t bufferSize = 4096;
    std::vector<char> buffer(bufferSize);

    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            sha256.add(buffer.data(), bytesRead);
        }
    }

    hash = sha256.getHash();
}

bool Sender::sendFileNamePacket(const std::string& fileName) {
    Packet fileNamePacket;
    fileNamePacket.type = FILESIZE;
    fileNamePacket.seqNum = 0;
    fileNamePacket.dataSize = fileName.size();
    std::strcpy(fileNamePacket.fileName, fileName.c_str());
    fileNamePacket.crc = CRC::Calculate(fileNamePacket.fileName, fileNamePacket.dataSize, CRC::CRC_32());

    buffer[0].packet = fileNamePacket;
    buffer[0].acknowledged = false;
    buffer[0].lastSent = std::chrono::steady_clock::now();

    std::cout << "Sending file name packet: " << fileName << std::endl;
    sendto(socketS, (char*)&packetError(fileNamePacket), sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    return handleACK(ANSWER_CRC, fileNamePacket.seqNum);
}

bool Sender::sendDataPackets(const std::string& filePath, const std::string& sha256Hash) {
    FILE* file_in = fopen(filePath.c_str(), "rb");
    if (!file_in) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return false;
    }

    seqNum = 1;
    Packet dataPacket;
    dataPacket.type = DATA;
    dataPacket.seqNum = seqNum;
    int i = 0;
    bool sendingDone = false;

    std::atomic<bool> sendingDoneFlag{ false };
    std::atomic<bool> stopAckListener{ false };

    std::thread ackListenerThread([&]() {
        while (!stopAckListener.load()) {
            sockaddr_in from;
            int fromlen = sizeof(from);
            Packet ackPacket;
            int recvLen = recvfrom(socketS, (char*)&ackPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen);

            if (recvLen != SOCKET_ERROR && ackPacket.type == ANSWER_CRC && isBufferAllNum(ackPacket.data, ackPacket.dataSize, 1)) {
                std::unique_lock<std::mutex> lock(ack_mutex);
                int seq = ackPacket.seqNum;
                int idx = seq % WINDOW_SIZE;

                if (seq >= base && seq < base + WINDOW_SIZE) {
                    if (!buffer[idx].acknowledged) {
                        std::cout << GREEN << "[ACK Thread] Received ACK for seqNum " << seq << RESET << std::endl;
                        buffer[idx].acknowledged = true;
                        ackReceived[seq] = true;

                        // Slide window
                        while (buffer[base % WINDOW_SIZE].acknowledged && base < seqNum) {
                            std::cout << BLUE << "Sliding window base to " << (base + 1) << RESET << std::endl;
                            base++;
                            ack_cv.notify_all(); // Notify all waiting threads
                        }
                    }
                }
            }
        }
        });

    std::thread resendThread([&]() {
        while (!sendingDoneFlag.load()) {
            std::unique_lock<std::mutex> lock(ack_mutex);
            int baseSnapshot = base.load();
            int nextSeqNumSnapshot = nextSeqNum.load();
            auto now = std::chrono::steady_clock::now();
            for (int j = baseSnapshot; j < nextSeqNumSnapshot; ++j) {
                int idx = j % WINDOW_SIZE;
                if (buffer[idx].packet.type == DATA &&
                    !buffer[idx].acknowledged &&
                    buffer[idx].packet.seqNum == j &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - buffer[idx].lastSent).count() > TIMEOUT_MS) {
                    std::cout << "Resending data packet " << buffer[idx].packet.seqNum << " due to timeout" << std::endl;
                    buffer[idx].lastSent = now;
                    sendto(socketS, (char*)&buffer[idx].packet, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        });

    int lastSentID;

    while (true) {
        char c;
        if (fread(&c, 1, 1, file_in) != 1) break;

        dataPacket.data[i++] = c;

        if (i >= BUFFERS_LEN) {
            std::unique_lock<std::mutex> lock(ack_mutex);

            while (nextSeqNum >= base + WINDOW_SIZE) {
                ack_cv.wait(lock);  // Wait for space in the window
            }

            dataPacket.seqNum = seqNum++;
            dataPacket.dataSize = i;
            dataPacket.crc = CRC::Calculate(dataPacket.data, dataPacket.dataSize, CRC::CRC_32());

            int idx = dataPacket.seqNum % WINDOW_SIZE;
            buffer[idx].packet = dataPacket;
            buffer[idx].acknowledged = false;
            buffer[idx].lastSent = std::chrono::steady_clock::now();
            lastSentID = idx;

            std::cout << YELLOW << "Sending data packet number " << dataPacket.seqNum << RESET << std::endl;
            sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
            nextSeqNum++;

            i = 0;
        }
    }

    // Final partial packet if remaining
    if (i != 0) {
        std::unique_lock<std::mutex> lock(ack_mutex);
        while (nextSeqNum >= base + WINDOW_SIZE) {
            ack_cv.wait(lock);
        }

        dataPacket.seqNum = seqNum++;
        dataPacket.dataSize = i;
        dataPacket.crc = CRC::Calculate(dataPacket.data, dataPacket.dataSize, CRC::CRC_32());

        int idx = dataPacket.seqNum % WINDOW_SIZE;
        buffer[idx].packet = dataPacket;
        buffer[idx].acknowledged = false;
        buffer[idx].lastSent = std::chrono::steady_clock::now();
        lastSentID = idx;

        std::cout << "Sending final partial packet " << dataPacket.seqNum << std::endl;
        sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
        nextSeqNum++;
    }

    // Wait for all packets to be acknowledged
    {
        std::unique_lock<std::mutex> lock(ack_mutex);
        ack_cv.wait(lock, [&]() {
            return buffer[lastSentID].acknowledged;
            });
    }

    // Send final packet
    Packet finPacket;
    finPacket.type = FINAL;
    finPacket.seqNum = seqNum;
    std::strcpy(finPacket.hashArray, sha256Hash.c_str());
    finPacket.dataSize = SHA256_LEN;
    finPacket.crc = CRC::Calculate(finPacket.hashArray, finPacket.dataSize, CRC::CRC_32());

    const int maxRetries = 3;
    int attempts = 0;

    std::cout << YELLOW << "Sending final packet (attempt " << (attempts + 1) << ")" << RESET << std::endl;
    sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
    while (attempts < maxRetries) {

        // Wait for CRC ACK
            if (!handleACK(ANSWER_CRC, finPacket.seqNum)) {
                std::cerr << RED << "CRC ACK not received or invalid. Retrying..." << RESET << std::endl;
                ++attempts;
                std::cout << YELLOW << "Sending final packet (attempt " << (attempts + 1) << ")" << RESET << std::endl;
                sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
                continue;
            }
        

        // Wait for SHA ACK
        if (handleACK(ANSWER_SHA, finPacket.seqNum)) {
                std::cout << GREEN << "SHA ACK received! File transfer successful." << RESET << std::endl;
                sendingDoneFlag.store(true);
                resendThread.join();
                stopAckListener = true;
                ackListenerThread.join();
                fclose(file_in);
                return true;
            }
            else {
                std::cerr << RED << "SHA mismatch! Restarting whole transfer..." << RESET << std::endl;
                sendingDoneFlag.store(true);
                resendThread.join();
                stopAckListener = true;
                ackListenerThread.join();
                fclose(file_in);
                return false;
            }
        
    }

    std::cerr << RED << "Final packet failed after " << maxRetries << " attempts!" << RESET << std::endl;
    sendingDoneFlag.store(true);
    resendThread.join();
    stopAckListener = true;
    ackListenerThread.join();
    fclose(file_in);
    return false;
}

bool Sender::sendFinalPacket(const std::string& hash) {
    Packet finPacket;
    finPacket.type = FINAL;
    finPacket.seqNum = seqNum;
    std::strcpy(finPacket.hashArray, hash.c_str());
    finPacket.dataSize = SHA256_LEN;
    finPacket.crc = CRC::Calculate(finPacket.hashArray, finPacket.dataSize, CRC::CRC_32());

    const int maxRetries = 3;
    int attempts = 0;

    while (attempts < maxRetries) {
        std::cout << YELLOW << "Sending final packet (attempt " << (attempts + 1) << ")" << RESET << std::endl;
        sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

        // Wait for CRC ACK
        if (!handleACK(ANSWER_CRC, finPacket.seqNum)) {
            std::cerr << RED << "CRC ACK not received or invalid. Retrying..." << RESET << std::endl;
            ++attempts;
            continue;
        }

        // Wait for SHA ACK
        if (handleACK(ANSWER_SHA, finPacket.seqNum)) {
            std::cout << GREEN << "SHA ACK received! File transfer successful." << RESET << std::endl;
            return true;
        }
        else {
            std::cerr << RED << "SHA mismatch! Restarting whole transfer..." << RESET << std::endl;
            return false;
        }
    }

    std::cerr << RED << "Final packet failed after " << maxRetries << " attempts!" << RESET << std::endl;
    return false;
}

bool Sender::handleACK(PacketType expectedType, int expectedSeqNum) {
    sockaddr_in from;
    int fromlen = sizeof(from);
    int attempts = 0;
    int maxRetries = 3;

    while (attempts < maxRetries) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketS, &readfds);

        timeval timeout;
        timeout.tv_sec = TIMEOUT_MS / 1000;
        timeout.tv_usec = (TIMEOUT_MS);

        int result = select(0, &readfds, nullptr, nullptr, &timeout);
        if (result > 0) {
            Packet ackPacket;
            int recvLen = recvfrom(socketS, (char*)&ackPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen);

            if (recvLen != SOCKET_ERROR) {
                if (ackPacket.type == expectedType && ackPacket.seqNum == expectedSeqNum) {
                    bool ackValid = isBufferAllNum(ackPacket.data, ackPacket.dataSize, 1);
                    if (ackValid) {
                        std::cout << GREEN << "[ACK] Valid ACK received for seqNum " << expectedSeqNum << RESET << std::endl;
                        return true;
                    }
                    else {
                        std::cerr << RED << "[ACK] Invalid ACK content (e.g., CRC/SHA fail) for seqNum " << expectedSeqNum << RESET << std::endl;
                        return false;  // ACK was received, but bad content, don't retry
                    }
                }
                else {
                    std::cerr << RED << "[ACK] Unexpected ACK: type " << ackPacket.type
                        << ", seqNum " << ackPacket.seqNum << " (expected " << expectedType << ", " << expectedSeqNum << ")" << RESET << std::endl;
                }
            }
            else {
                std::cerr << RED << "[ACK] recvfrom error!" << RESET << std::endl;
            }
        }
        else if (result == 0) {
            std::cerr << YELLOW << "[ACK] Timeout waiting for seqNum " << expectedSeqNum
                << " (attempt " << (attempts + 1) << "/" << maxRetries << ")" << RESET << std::endl;
        }
        else {
            std::cerr << RED << "[ACK] select() failed!" << RESET << std::endl;
            break;
        }

        ++attempts;
    }

    std::cerr << RED << "[ACK] Failed to receive ACK for seqNum " << expectedSeqNum << " after " << maxRetries << " attempts." << RESET << std::endl;
    return false;
}
