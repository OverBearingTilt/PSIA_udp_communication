// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
//

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include <winsock2.h>
#include "ws2tcpip.h"

#define TARGET_IP	"147.32.221.16"
// 147.32.216.175

#define BUFFERS_LEN 1024
#define NAME_LEN 64


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
	char type; // 0 == FILSIZE, 1 == DATA, 2 == FINAL
	int seqNum;
	int dataSize;
	union {
		char fileName[NAME_LEN];
		char data[BUFFERS_LEN];
	};
	//int crc;

} Packet;


typedef enum {
	FILESIZE,
	DATA,
	FINAL
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


//**********************************************************************
int main()
{
	SOCKET socketS;
	
	InitWinsock();

	struct sockaddr_in local;
	struct sockaddr_in from;

	int fromlen = sizeof(from);
	local.sin_family = AF_INET;
	local.sin_port = htons(LOCAL_PORT);
	local.sin_addr.s_addr = INADDR_ANY;


	socketS = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0){
		printf("Binding error!\n");
	    getchar(); //wait for press Enter
		return 1;
	}
	//**********************************************************************
	char buffer_rx[BUFFERS_LEN];
	char buffer_tx[BUFFERS_LEN];

#ifdef SENDER
	
	sockaddr_in addrDest;
	addrDest.sin_family = AF_INET;
	addrDest.sin_port = htons(TARGET_PORT);
	InetPton(AF_INET, _T(TARGET_IP), &addrDest.sin_addr.s_addr);

	char* FILENAME = "randomPlane.jpg";

	// load the file to be sent
	FILE* file_in = fopen("C:/Programovani/PSIA/Images/randomPlane.jpg", "rb");

	// send first packet
	Packet fileNamePacket;
	
	fileNamePacket.type = FILESIZE;
	fileNamePacket.seqNum = 0;
	fileNamePacket.dataSize = strlen(FILENAME);
	printf("%d", fileNamePacket.dataSize);
	strcpy(fileNamePacket.fileName, FILENAME);

	printf("sending file name packet: %s\n", fileNamePacket.fileName);
	sendto(socketS, (char*)&fileNamePacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

	// send data
	Packet dataPacket;

	dataPacket.type = DATA;
	dataPacket.seqNum = 1;
	int i = 0;
	for (char c = 0; fread(&c, 1, 1, file_in) == 1; ) { // maybe read entire buffersize?
		if (i >= BUFFERS_LEN) {
			// send packet and reset data
			dataPacket.dataSize = i;
			printf("sending data packet number %d\n", dataPacket.seqNum);
			sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
			//reset_data(dataPacket.data);
			i = 0;
			dataPacket.seqNum++;
			// wait
			Sleep(50);
		}
		dataPacket.data[i] = c;
		i++;
	}
	fclose(file_in);
	if (i != 0) {
		// send the remaining data
		dataPacket.dataSize = i;
		printf("sending remainder data packet number %d\n", dataPacket.seqNum);
		sendto(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));
		dataPacket.seqNum++;
	}
	
	// send final packet
	printf("sending finish packet number %d\n", dataPacket.seqNum);
	Packet finPacket;
	finPacket.type = FINAL;
	finPacket.seqNum = dataPacket.seqNum;
	finPacket.dataSize = 0;
	sendto(socketS, (char*)&finPacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));

	//strncpy(buffer_tx, "Hello world payload!\n", BUFFERS_LEN); //put some data to buffer
	//printf("Sending packet.\n");
	//sendto(socketS, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));	

	closesocket(socketS);

#endif // SENDER

#ifdef RECEIVER

	Packet fileNamePacket;
	char* fileName;
	FILE* file_out;
	bool continue_listening = false;
	printf("waiting for a packet\n");
	if (recvfrom(socketS, (char*)&fileNamePacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
		printf("Socket error!\n");
		getchar();
		return 1;
	}
	else {
		printf("First packet received!\n");
		fileName = (char*)malloc(fileNamePacket.dataSize*sizeof(char)+1);
		if (fileName == NULL) {
			printf("Malloc failed!\n");
			return 100;
		}
		for (int i = 0; i < fileNamePacket.dataSize; i++) {
			fileName[i] = fileNamePacket.fileName[i];
		}
		fileName[fileNamePacket.dataSize] = '\0';
		file_out = fopen(fileName, "wb");
		printf("receiving file named: %s\n", fileName);
		continue_listening = true;
	}
	free(fileName);

	Packet dataPacket;
	while (continue_listening) {
		if (recvfrom(socketS, (char*)&dataPacket, sizeof(Packet), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
			printf("Socket error!\n");
			getchar();
			return 1;
		}
		else {
			printf("Packet received!, number : %d\n", dataPacket.seqNum);
			if (dataPacket.type == DATA) {
				fwrite(dataPacket.data, dataPacket.dataSize, 1, file_out);
			} else if (dataPacket.type == FINAL) {
				continue_listening = false;
			}
		}
	}
	printf("Finished\n");
	fclose(file_out);

	//strncpy(buffer_rx, "No data received.\n", BUFFERS_LEN);
	//printf("Waiting for datagram ...\n");
	//if(recvfrom(socketS, buffer_rx, sizeof(buffer_rx), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR){
	//	printf("Socket error!\n");
	//	getchar();
	//	return 1;
	//}
	//else
	//	printf("Datagram: %s", buffer_rx);

	closesocket(socketS);
#endif
	//**********************************************************************

	getchar(); //wait for press Enter
	return 0;
}
