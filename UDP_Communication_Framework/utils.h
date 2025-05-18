#ifndef UTILS_H
#define UTILS_H

#include <cstdint>
#include <WinSock2.h>
#include <algorithm>
#include <string>
#include <Shlwapi.h>


// funny collors for log messages UwU
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define RESET "\x1b[0m"

#define BUFFERS_LEN 1024 - sizeof(uint32_t) - 2*sizeof(int) - sizeof(char)
#define NAME_LEN 64
#define SHA256_LEN 64
#define TOLERANCE 100

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

void InitWinsock();
void setBufferToNum(char* buffer, size_t size, char setTo);
bool isBufferAllNum(const char* buffer, int size, char setTo);
std::string WideToUtf8(const std::wstring& wideStr);
bool OpenFileDialog(std::string& filePath, std::string& fileName);
void reset_data(char data[BUFFERS_LEN]);
std::string stripQuotes(const std::string& input);
std::string toLowerCase(const std::string& input);


#endif // UTILS_H
