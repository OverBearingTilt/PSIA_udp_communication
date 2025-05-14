// File: Receiver.cpp
#include "stdafx.h"
#include "Receiver.h"

Receiver::Receiver(int local_port, int target_port, wchar_t* target_IP) {
    InitWinsock();

    local.sin_family = AF_INET;
    local.sin_port = htons(local_port);
    local.sin_addr.s_addr = INADDR_ANY;

    socketS = socket(AF_INET, SOCK_DGRAM, 0);
    if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0) {
        printf("%sBinding error!\n%s", RED, RESET);
        throw std::runtime_error("Binding failed");
    }

    addrDest.sin_family = AF_INET;
    addrDest.sin_port = htons(target_port);
    InetPton(AF_INET, target_IP, &addrDest.sin_addr.s_addr);
    // originaly _T(target_IP) which worked only on literals

}

Receiver::~Receiver() {
    if (file_out) fclose(file_out);
    if (fileName) free(fileName);
    closesocket(socketS);
}

int Receiver::run() {
    InitWinsock();

    Packet packet;

    while (retryCounter < 3 && !sha256_ok) {
        printf("Waiting for a packet\n");

        // Receive packet
        if (recvfrom(socketS, (char*)&packet, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
            printf("%sSocket error!\n%s", RED, RESET);
            return 1;
        }

        // Check for duplicate packets
        if (packet.seqNum == lastSeqNum) {
            handleDuplicatePacket(packet);
            continue;
        }

        // Update last sequence number
        lastSeqNum = packet.seqNum;

        // Handle packet based on its type
        switch (packet.type) {
        case FILESIZE:
            handleFileSizePacket(packet);
            break;
        case DATA:
            handleDataPacket(packet);
            break;
        case FINAL:
            handleFinalPacket(packet);
            break;
        default:
            printf("%sUnknown packet type received!%s\n", RED, RESET);
            break;
        }
    }

    printf("%sTransfer %s\n%s", (sha256_ok && retryCounter < 3) ? GREEN : RED, (sha256_ok) ? "Finished OK" : "Failed", RESET);
    closesocket(socketS);

    return sha256_ok ? 0 : 1;
}

// Function to handle duplicate packets
void Receiver::handleDuplicatePacket(const Packet& packet) {
    printf("%sDuplicate packet %d received, resending ACK%s - ", YELLOW, packet.seqNum, RESET);

    Packet answerPacket;
    answerPacket.type = ANSWER_CRC;
    answerPacket.seqNum = packet.seqNum;
    answerPacket.dataSize = packet.dataSize;

    if (crc_fail) {
        if (packet.type == DATA) {
            handleDataPacket(packet);
        }
        else if (packet.type == FINAL) {
            handleFinalPacket(packet);
        }
    }
    else {
        setBufferToNum(answerPacket.data, answerPacket.dataSize, 1); // Assume ACK ok for duplicate
        sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
    }
}

// Function to handle FILESIZE packets
void Receiver::handleFileSizePacket(const Packet& packet) {
    Packet answerPacket;
    answerPacket.type = ANSWER_CRC;
    answerPacket.seqNum = packet.seqNum;
    answerPacket.dataSize = sizeof(answerPacket.data);

    std::uint32_t crc = CRC::Calculate(packet.fileName, packet.dataSize, CRC::CRC_32());
    char ackResult = (crc == packet.crc) ? 1 : 0;

    setBufferToNum(answerPacket.data, sizeof(answerPacket.data), ackResult);
    printf("%sSending ACK packet: %d for seqNum: %d\n%s", (ackResult) ? GREEN : RED, ackResult, packet.seqNum, RESET);
    sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    if (ackResult) {
        // Allocate file name
        fileName = (char*)malloc(packet.dataSize + 1);
        if (!fileName) {
            printf("%sMalloc failed!\n%s", RED, RESET);
            throw std::runtime_error("Memory allocation failed for file name");
        }
        memcpy(fileName, packet.fileName, packet.dataSize);
        fileName[packet.dataSize] = '\0';
        printf("%sReceiving file: %s\n%s", YELLOW, fileName, RESET);

        file_out = fopen(fileName, "wb");
        if (!file_out) {
            printf("%sFailed to open file for writing!\n%s", RED, RESET);
            free(fileName);
            throw std::runtime_error("Failed to open file for writing");
        }
    }
}

// Function to handle DATA packets
void Receiver::handleDataPacket(const Packet& packet) {
    printf("Packet received!, number : %d\n", packet.seqNum);

    Packet answerPacket;
    answerPacket.type = ANSWER_CRC;
    answerPacket.seqNum = packet.seqNum;
    answerPacket.dataSize = packet.dataSize;

    std::uint32_t crc = CRC::Calculate(packet.data, packet.dataSize, CRC::CRC_32());
    char ackResult = (crc == packet.crc) ? 1 : 0;

    if (ackResult) {
        // Store the packet in the buffer at the position corresponding to its sequence number
        int bufferIndex = packet.seqNum % WINDOW_SIZE;
        buffer[bufferIndex].packet = packet;
        buffer[bufferIndex].arrived = true;

        // If this packet is in order (seqNum == baseSeqNum), write it to the file
        if (packet.seqNum == baseSeqNum + 1) {
            // Write the packet data to the file
            fwrite(packet.data, packet.dataSize, 1, file_out);
            crc_fail = false;

            // Move baseSeqNum forward and write all packets in order
            while (buffer[(baseSeqNum + 1) % WINDOW_SIZE].arrived) {
                baseSeqNum++; // Slide the window
                fwrite(buffer[(baseSeqNum) % WINDOW_SIZE].packet.data, buffer[(baseSeqNum) % WINDOW_SIZE].packet.dataSize, 1, file_out);
                buffer[(baseSeqNum) % WINDOW_SIZE].arrived = false; // Reset the slot after writing
            }
        }
    }
    else {
        crc_fail = true;
    }


    setBufferToNum(answerPacket.data, answerPacket.dataSize, ackResult);
    printf("%sSending ACK packet: %d for seqNum: %d\n%s", (ackResult) ? GREEN : RED, ackResult, packet.seqNum, RESET);
    sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
}

// Function to handle FINAL packets
void Receiver::handleFinalPacket(const Packet& packet) {
    printf("Final packet received!, number : %d\n", packet.seqNum);

    Packet answerPacket;
    answerPacket.type = ANSWER_CRC;
    answerPacket.seqNum = packet.seqNum;
    answerPacket.dataSize = BUFFERS_LEN;

    received_sha256 = packet.hashArray;
    std::uint32_t crc = CRC::Calculate(packet.hashArray, packet.dataSize, CRC::CRC_32());
    char ackResult = (crc == packet.crc) ? 1 : 0;

    setBufferToNum(answerPacket.data, answerPacket.dataSize, ackResult);
    printf("%sSending final CRC ACK: %d\n%s", (ackResult) ? GREEN : RED, ackResult, RESET);
    sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

    if (ackResult) {
        fclose(file_out);

        // Calculate SHA256
        std::ifstream file(fileName, std::ios::binary);
        const std::size_t bufferSize = 4096;
        std::vector<char> buffer(bufferSize);

        while (file) {
            file.read(buffer.data(), buffer.size());
            std::streamsize bytesRead = file.gcount();
            if (bytesRead > 0) sha256.add(buffer.data(), bytesRead);
        }
        file.close();

        sha256_ok = (strcmp(sha256.getHash().c_str(), received_sha256) == 0);
        printf("%sSHA256 %s\n%s", sha256_ok ? GREEN : RED, sha256_ok ? "OK" : "NOK", RESET);

        // Send SHA result
        answerPacket.type = ANSWER_SHA;
        answerPacket.seqNum = packet.seqNum + 1;
        setBufferToNum(answerPacket.data, answerPacket.dataSize, sha256_ok);
        printf("%sSending SHA result ACK: %d\n%s", (sha256_ok) ? GREEN : RED, sha256_ok, RESET);
        sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

        retryCounter += (sha256_ok ? 0 : 1);
        lastSeqNum = -1;
    }
    else {
        crc_fail = true;
    }

    free(fileName);
    fileName = nullptr;
}