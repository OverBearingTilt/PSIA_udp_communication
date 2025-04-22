// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
//

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include <winsock2.h>
#include "ws2tcpip.h"
#include <algorithm>
#include "CRC.h"
#include <iostream>
#include <cstdint>
#include <filesystem>
#include <string>
#include <fstream>
#include <vector>
#include "sha256.h"

#define TARGET_IP	"127.0.0.1"
// 147.32.216.175

#define BUFFERS_LEN 1024 - sizeof(uint32_t) - 2*sizeof(int) - sizeof(char)
#define NAME_LEN 64
#define SHA256_LEN 64
#define TOLERANCE 100
#define TIMEOUT_SEC 1

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define RESET "\x1b[0m"

//#define SENDER
#define RECEIVER

#ifdef SENDER
#define TARGET_PORT 5001
#define LOCAL_PORT 5000
#endif // SENDER

#ifdef RECEIVER
#define TARGET_PORT 5000
#define LOCAL_PORT 5001
#endif // RECEIVER


typedef struct {
	char type; // 0 == FILSIZE, 1 == DATA, 2 == FINAL, 3 == ANSWER
	int seqNum;
	int dataSize;
	union {
		char fileName[NAME_LEN];
		char data[BUFFERS_LEN];
		char hashArray[SHA256_LEN + 1]; //64 + 1
	};
	uint32_t crc;
} Packet;

typedef enum {
	FILESIZE,
	DATA,
	FINAL,
	ANSWER_CRC,  // ones - OK, zeroes - send again
	ANSWER_SHA
} PacketType;

void InitWinsock()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
}

void reset_data(char data[BUFFERS_LEN]) {
	for (int i = 0; i < BUFFERS_LEN; i++) {
		data[i] = 0;
	}
}

void setBufferToNum(char* buffer, size_t size, char setTo) {
	std::fill(buffer, buffer + size, static_cast<char>(setTo));
}

bool isBufferAllNum(const char* buffer, int size, char setTo) {
	int errorCount = 0;
	for (int i = 0; i < size; i++) {
		if (buffer[i] != static_cast<char>(setTo)) {
			++errorCount;
			if (errorCount > TOLERANCE) {
				return false;
			}
		}
	}
	return true;
}

std::string stripQuotes(const std::string& input) {
	std::string result = input;
	if (!result.empty() && result.front() == '\"') {
		result.erase(0, 1);
	}
	if (!result.empty() && result.back() == '\"') {
		result.pop_back();
	}
	return result;
}
//----------------------------------------------------------------------------------------------------------
//---------------------------------------------runReceiver--------------------------------------------------
//----------------------------------------------------------------------------------------------------------
int runReceiver() {
	SOCKET socketS;
	SHA256 sha256;
	bool sha256_ok = false;
	int retryCounter = 0;
	int lastSeqNum = -1; //track last processed packet seqNum

	InitWinsock();

	struct sockaddr_in local;
	struct sockaddr_in from;
	int fromlen = sizeof(from);
	local.sin_family = AF_INET;
	local.sin_port = htons(LOCAL_PORT);
	local.sin_addr.s_addr = INADDR_ANY;

	socketS = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0) {
		printf("%sBinding error!\n%s", RED, RESET);
		return 1;
	}

	sockaddr_in addrDest;
	addrDest.sin_family = AF_INET;
	addrDest.sin_port = htons(TARGET_PORT);
	InetPton(AF_INET, _T(TARGET_IP), &addrDest.sin_addr.s_addr);

	Packet packet;
	char* fileName = nullptr;
	FILE* file_out = nullptr;
	const char* received_sha256 = nullptr;

	while (retryCounter < 3 && !sha256_ok) {
		printf("Waiting for a packet\n");

		if (recvfrom(socketS, (char*)&packet, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
			printf("%sSocket error!\n%s", RED, RESET);
			return 1;
		}

		if (packet.seqNum == lastSeqNum) {
			printf("%sDuplicate packet %d received, resending ACK\n%s", YELLOW, packet.seqNum, RESET);

			Packet answerPacket;
			answerPacket.type = ANSWER_CRC;
			answerPacket.seqNum = packet.seqNum;
			answerPacket.dataSize = packet.dataSize;
			setBufferToNum(answerPacket.data, answerPacket.dataSize, 1); // assume ACK ok for duplicate
			sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

			continue;
		}

		lastSeqNum = packet.seqNum;

		if (packet.type == FILESIZE) {
			//allocate file name
			fileName = (char*)malloc(packet.dataSize + 1);
			if (!fileName) {
				printf("%sMalloc failed!\n%s", RED, RESET);
				return 100;
			}
			memcpy(fileName, packet.fileName, packet.dataSize);
			fileName[packet.dataSize] = '\0';
			printf("%sReceiving file: %s\n%s", YELLOW, fileName, RESET);

			file_out = fopen(fileName, "wb");
			if (!file_out) {
				printf("%sFailed to open file for writing!\n%s", RED, RESET);
				free(fileName);
				return 1;
			}
		}

		if (packet.type == DATA) {
			printf("Packet received!, number : %d\n", packet.seqNum);
			Packet answerPacket;
			answerPacket.type = ANSWER_CRC;
			answerPacket.seqNum = packet.seqNum;
			answerPacket.dataSize = packet.dataSize;

			std::uint32_t crc = CRC::Calculate(packet.data, packet.dataSize, CRC::CRC_32());
			char ackResult = (crc == packet.crc) ? 1 : 0;

			if (ackResult) fwrite(packet.data, packet.dataSize, 1, file_out);

			setBufferToNum(answerPacket.data, answerPacket.dataSize, ackResult);
			printf("%sSending ACK packet: %d for seqNum: %d\n%s", (ackResult) ? GREEN : RED, ackResult, packet.seqNum, RESET);
			sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
		}

		if (packet.type == FINAL) {
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

				//xalculate sha256
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

				//send SHA result
				answerPacket.type = ANSWER_SHA;
				answerPacket.seqNum = packet.seqNum + 1;
				setBufferToNum(answerPacket.data, answerPacket.dataSize, sha256_ok);
				printf("%sSending SHA result ACK: %d\n%s", (sha256_ok) ? GREEN : RED, sha256_ok, RESET);
				sendto(socketS, (char*)&answerPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

				retryCounter += (sha256_ok ? 0 : 1);
			}
			else {
				retryCounter++;
			}

			free(fileName);
			fileName = nullptr;
		}
	}

	printf("%sTransfer %s\n%s", (sha256_ok && retryCounter < 3) ? GREEN : RED, (sha256_ok) ? "Finished OK" : "Failed", RESET);
	closesocket(socketS);
	return 0;
}

//----------------------------------------------------------------------------------------------------------
//--------------------------------------------/runReceiver--------------------------------------------------
//----------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------
//---------------------------------------------runSender----------------------------------------------------
//----------------------------------------------------------------------------------------------------------
int runSender() {
	SOCKET socketS;
	SHA256 sha256;
	bool sha256_ok = false;
	int counter = 0;

	InitWinsock();

	struct sockaddr_in local;
	struct sockaddr_in from;
	int fromlen = sizeof(from);
	local.sin_family = AF_INET;
	local.sin_port = htons(LOCAL_PORT);
	local.sin_addr.s_addr = INADDR_ANY;

	socketS = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0) {
		printf("%sBinding error!\n%s", RED, RESET);
		return 1;
	}

	sockaddr_in addrDest;
	addrDest.sin_family = AF_INET;
	addrDest.sin_port = htons(TARGET_PORT);
	InetPton(AF_INET, _T(TARGET_IP), &addrDest.sin_addr.s_addr);

	std::string strFilePath;
	std::cout << "Enter the path to the file: ";
	std::getline(std::cin, strFilePath);

	stripQuotes(strFilePath);

	std::filesystem::path pathObj(strFilePath);
	std::string strFileName = pathObj.filename().string();

	const char* FILENAME = strFileName.c_str();
	const char* FILEPATH = strFilePath.c_str();

	//sha256 calculation
	{
		std::ifstream file(FILEPATH, std::ios::binary);
		const std::size_t bufferSize = 4096;
		std::vector<char> buffer(bufferSize);
		while (file) {
			file.read(buffer.data(), buffer.size());
			std::streamsize bytesRead = file.gcount();
			if (bytesRead > 0) sha256.add(buffer.data(), bytesRead);
		}
		file.close();
	}

	//send filename packet
	Packet fileNamePacket;
	fileNamePacket.type = FILESIZE;
	fileNamePacket.seqNum = 0;
	fileNamePacket.dataSize = strlen(FILENAME);
	fileNamePacket.crc = CRC::Calculate(fileNamePacket.data, fileNamePacket.dataSize, CRC::CRC_32());
	strcpy(fileNamePacket.fileName, FILENAME);
	printf("Sending file name packet: %s\n", fileNamePacket.fileName);
	sendto(socketS, (char*)&fileNamePacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

	//send data
	FILE* file_in = fopen(FILEPATH, "rb");
	Packet dataPacket;
	Packet ansPacket;
	dataPacket.type = DATA;
	dataPacket.seqNum = 1;
	int i = 0;

	for (char c = 0; fread(&c, 1, 1, file_in) == 1; ) {
		if (i >= BUFFERS_LEN) {
			dataPacket.dataSize = i;
			dataPacket.crc = CRC::Calculate(dataPacket.data, dataPacket.dataSize, CRC::CRC_32());
			printf("Sending data packet number %d\n", dataPacket.seqNum);
			sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));



			//wait for ACK
			//now with timeout :3
			bool delivered = false;
			while (!delivered) {
				printf("Waiting for answer\n");

				fd_set readfds;
				FD_ZERO(&readfds);
				FD_SET(socketS, &readfds);

				struct timeval timeout;
				timeout.tv_sec = TIMEOUT_SEC;
				timeout.tv_usec = 0;

				int selectResult = select(0, &readfds, NULL, NULL, &timeout);
				if (selectResult > 0) {
					if (recvfrom(socketS, (char*)&ansPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
						printf("%sSocket error!\n%s", RED, RESET);
						return 1;
					}
					delivered = !isBufferAllNum(ansPacket.data, ansPacket.dataSize, 0);
				}
				else if (selectResult == 0) {
					printf("%sACK timeout for packet %d, resending...\n%s", YELLOW, dataPacket.seqNum, RESET);
					sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
				}
				else {
					printf("%sSelect error!\n%s", RED, RESET);
					return 1;
				}
			}
			i = 0;
			dataPacket.seqNum++;
		}
		dataPacket.data[i++] = c;
	}

	//send remaining data if any
	if (i != 0) {
		dataPacket.dataSize = i;
		dataPacket.crc = CRC::Calculate(dataPacket.data, dataPacket.dataSize, CRC::CRC_32());
		printf("Sending remainder data packet number %d\n", dataPacket.seqNum);
		sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

		//wait for ACK
		//now with timeout :3
		bool delivered = false;
		while (!delivered) {
			printf("Waiting for answer\n");

			fd_set readfds;
			FD_ZERO(&readfds);
			FD_SET(socketS, &readfds);

			struct timeval timeout;
			timeout.tv_sec = TIMEOUT_SEC;
			timeout.tv_usec = 0;

			int selectResult = select(0, &readfds, NULL, NULL, &timeout);
			if (selectResult > 0) {
				if (recvfrom(socketS, (char*)&ansPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
					printf("%sSocket error!\n%s", RED, RESET);
					return 1;
				}
				delivered = !isBufferAllNum(ansPacket.data, ansPacket.dataSize, 0);
			}
			else if (selectResult == 0) {
				printf("%sACK timeout for packet %d, resending...\n%s", YELLOW, dataPacket.seqNum, RESET);
				sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
			}
			else {
				printf("%sSelect error!\n%s", RED, RESET);
				return 1;
			}
		}
		dataPacket.seqNum++;
	}

	fclose(file_in);

	//send final packet
	Packet finPacket;
	finPacket.type = FINAL;
	finPacket.seqNum = dataPacket.seqNum;
	std::strcpy(finPacket.hashArray, sha256.getHash().c_str());
	finPacket.dataSize = SHA256_LEN;
	finPacket.crc = CRC::Calculate(finPacket.hashArray, finPacket.dataSize, CRC::CRC_32());
	printf("%sSending finish packet number %d\n%s", YELLOW, finPacket.seqNum, RESET);
	sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

	//wait for both CRC and SHA ACK
	bool finalCRC_OK = false;
	bool finalSHA_OK = false;

	while (!(finalCRC_OK && finalSHA_OK)) {
		printf("Waiting for final answers\n");

		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(socketS, &readfds);

		struct timeval timeout;
		timeout.tv_sec = TIMEOUT_SEC;
		timeout.tv_usec = 0;

		int selectResult = select(0, &readfds, NULL, NULL, &timeout);
		if (selectResult > 0) {
			if (recvfrom(socketS, (char*)&ansPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
				printf("%sSocket error!\n%s", RED, RESET);
				return 1;
			}
			if (ansPacket.type == ANSWER_CRC)
				finalCRC_OK = !isBufferAllNum(ansPacket.data, ansPacket.dataSize, 0);
			if (ansPacket.type == ANSWER_SHA) {
				finalSHA_OK = !isBufferAllNum(ansPacket.data, ansPacket.dataSize, 0);
				sha256_ok = finalSHA_OK;
				(sha256_ok) ? printf("sha256 OK\n") : printf("sha256 NOK\n");
			}
		}
		else if (selectResult == 0) {
			// timeout, resend final packet
			printf("%sACK timeout for final packet %d, resending...\n%s", YELLOW, finPacket.seqNum, RESET);
			sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
		}
		else {
			printf("%sSelect error!\n%s", RED, RESET);
			return 1;
		}
	}


	closesocket(socketS);
	return 0;
}

//----------------------------------------------------------------------------------------------------------
//--------------------------------------------/runSender----------------------------------------------------
//----------------------------------------------------------------------------------------------------------

std::string toLowerCase(const std::string& input) {
	std::string result = input;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return result;
}

int main(int argc, char* argv[])
{
	int retValue = -1;


#ifdef SENDER
	retValue = runSender();
#endif // SENDER

#ifdef RECEIVER
	retValue = runReceiver();
#endif

	/*
	std::string mode;
	if (argc == 2) {
		mode = argv[1];
	} else {
		return -1;
	}
	if (mode == "sender") {
		runSender();
	}
	else if (mode == "receiver") {
		runReceiver();
	}
	else {
		std::cout << "Unknown mode: " << mode << std::endl;
		return 1;
	}
	*/

	getchar(); //wait for press Enter
	return retValue;
}
