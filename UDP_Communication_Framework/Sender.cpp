#include "stdafx.h"
#include "Sender.h"

#pragma comment(lib, "ws2_32.lib")


Sender::Sender(int local_port, int target_port, wchar_t* target_IP) {
    InitWinsock();

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

int Sender::run(const std::string& filePath, const std::string& fileName) {
    try {
        std::string sha256Hash;
        calculateSHA256(filePath, sha256Hash);

        if (!sendFileNamePacket(fileName)) {
            return 1;
        }

        if (!sendDataPackets(filePath)) {
            return 1;
        }

        if (!sendFinalPacket(sha256Hash)) {
            return 1;
        }

        return 0;
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

    std::cout << "Sending file name packet: " << fileName << std::endl;
    sendto(socketS, (char*)&fileNamePacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    return waitForACK(ANSWER_CRC, fileNamePacket.seqNum);
}

bool Sender::sendDataPackets(const std::string& filePath) {
    FILE* file_in = fopen(filePath.c_str(), "rb");
    if (!file_in) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return false;
    }

    Packet dataPacket;
    dataPacket.type = DATA;
    dataPacket.seqNum = 1;
    int i = 0;

    for (char c = 0; fread(&c, 1, 1, file_in) == 1; ) {
        if (i >= BUFFERS_LEN) {
            dataPacket.dataSize = i;
            dataPacket.crc = CRC::Calculate(dataPacket.data, dataPacket.dataSize, CRC::CRC_32());
            std::cout << "Sending data packet number " << dataPacket.seqNum << std::endl;
            sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

            if (!waitForACK(ANSWER_CRC, dataPacket.seqNum)) {
                fclose(file_in);
                return false;
            }

            i = 0;
            dataPacket.seqNum++;
        }
        dataPacket.data[i++] = c;
    }

    if (i != 0) {
        dataPacket.dataSize = i;
        dataPacket.crc = CRC::Calculate(dataPacket.data, dataPacket.dataSize, CRC::CRC_32());
        std::cout << "Sending remainder data packet number " << dataPacket.seqNum << std::endl;
        sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

        if (!waitForACK(ANSWER_CRC, dataPacket.seqNum)) {
            fclose(file_in);
            return false;
        }
    }

    fclose(file_in);
    return true;
}

bool Sender::sendFinalPacket(const std::string& hash) {
    Packet finPacket;
    finPacket.type = FINAL;
    finPacket.seqNum = 0;
    std::strcpy(finPacket.hashArray, hash.c_str());
    finPacket.dataSize = SHA256_LEN;
    finPacket.crc = CRC::Calculate(finPacket.hashArray, finPacket.dataSize, CRC::CRC_32());

    std::cout << "Sending final packet" << std::endl;
    sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    return waitForACK(ANSWER_SHA, finPacket.seqNum);
}

bool Sender::waitForACK(int packetType, int seqNum) {
    Packet ansPacket;
    sockaddr_in from;
    int fromlen = sizeof(from);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socketS, &readfds);

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;

    int selectResult = select(0, &readfds, NULL, NULL, &timeout);
    if (selectResult > 0) {
        if (recvfrom(socketS, (char*)&ansPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
            std::cerr << "Socket error while waiting for ACK" << std::endl;
            return false;
        }
        return ansPacket.type == packetType && ansPacket.seqNum == seqNum;
    }
    else if (selectResult == 0) {
        std::cerr << "ACK timeout for packet " << seqNum << std::endl;
        return false;
    }
    else {
        std::cerr << "Select error while waiting for ACK" << std::endl;
        return false;
    }
}
