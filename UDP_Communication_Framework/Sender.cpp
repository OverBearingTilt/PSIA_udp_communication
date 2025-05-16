#include "stdafx.h"
#include "Sender.h"


#pragma comment(lib, "ws2_32.lib")


Sender::Sender(int local_port, int target_port, wchar_t* target_IP) {
    InitWinsock();

    base = 0;
    nextSeqNum = 0;

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
    // originaly _T(target_IP) which worked only on literals

}

Sender::~Sender() {
    closesocket(socketS);
    WSACleanup();
}

void Sender::waitForAcksThread() {
    while (!stopThreads.load()) {
        Packet ackPacket;
        sockaddr_in from;
        int fromLen = sizeof(from);

		for (int i = 0; i < WINDOW_SIZE; ++i) {
			if (buffer[i].packet.type == FILESIZE) {
				buffer[i].acknowledged = true;
				ackReceived[i] = true;
			}
		}

        if (recvfrom(socketS, (char*)&ackPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromLen) > 0) {
            if (ackPacket.type == ANSWER_CRC) {
                std::unique_lock<std::mutex> lock(ack_mutex);
                int seq = ackPacket.seqNum;
                ackReceived[seq % WINDOW_SIZE] = true;
                buffer[seq % WINDOW_SIZE].acknowledged = true;
                ack_cv.notify_all();
                std::cout << "ACK received for packet: " << seq << std::endl;

                // Slide the window
                while (ackReceived[base % WINDOW_SIZE]) {
                    ackReceived[base % WINDOW_SIZE] = false;
                    base++;
                }
            }
        }
    }
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

            if (!sendDataPackets(filePath)) {
                ++try_counter;
                continue;
            }

            if (sendFinalPacket(sha256Hash)) {
                sha_ok = true;
            }
            else {
                ++try_counter;
            }
        }

        return !sha_ok;

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

    seqNum = 1;
    Packet dataPacket;
    dataPacket.type = DATA;
    dataPacket.seqNum = seqNum;
    int i = 0;
    bool sendingDone = false;

    //std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::thread ackThread(&Sender::waitForAcksThread, this);

    std::atomic<bool> sendingDoneFlag{ false }; // Use atomic for thread-safe signaling
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
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - buffer[idx].lastSent).count() > TIMEOUT_MS) {
                    std::cout << "Resending data packet " << buffer[idx].packet.seqNum << " due to timeout" << std::endl;
                    buffer[idx].lastSent = now;
                    sendto(socketS, (char*)&buffer[idx].packet, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        });


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

            std::cout << "Sending data packet number " << dataPacket.seqNum << std::endl;
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

        std::cout << "Sending final partial packet " << dataPacket.seqNum << std::endl;
        sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
        nextSeqNum++;
    }

	// wait for all packets to be acknowledged
	while (base <= nextSeqNum) {
		// wait
		std::chrono::milliseconds waitTime(50);
	}

    sendingDone = true; 
    stopThreads.store(true);
    //if (ackThread.joinable())
        ackThread.join();
	sendingDoneFlag.store(true); // Signal the resend thread to stop
    resendThread.join();

    fclose(file_in);
    return true;

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
        //wait for CRC
        if (!waitForACK(ANSWER_CRC, finPacket.seqNum)) {
            std::cerr << RED << "CRC ACK not received or invalid. Retrying..." << RESET << std::endl;
            ++attempts;
            continue;
        }
        //wait for SHA
        if (waitForACK(ANSWER_SHA, finPacket.seqNum)) {
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

bool Sender::waitForACK(int packetType, int seqNum) {
    for (int retries = 0; retries < 3; ++retries) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketS, &readfds);

        timeval timeout{ 0, TIMEOUT_MS * 1000 };

        int selectResult = select(0, &readfds, NULL, NULL, &timeout);
        if (selectResult > 0) {
            Packet ansPacket;
            sockaddr_in from;
            int fromlen = sizeof(from);

            if (recvfrom(socketS, (char*)&ansPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) != SOCKET_ERROR) {
                if (ansPacket.type == packetType && ansPacket.seqNum == seqNum) {
                    // should check the ACK data - TODO
                    return true;
                }
            }
        }
        else if (selectResult == 0) {
            std::cerr << "Timeout waiting for ACK. Retrying..." << std::endl;
        }
        else {
            std::cerr << "Select error while waiting for ACK" << std::endl;
            break;
        }
    }

    return false;
}
