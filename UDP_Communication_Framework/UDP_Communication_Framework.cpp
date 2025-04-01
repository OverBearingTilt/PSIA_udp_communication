// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
//

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include <winsock2.h>
#include "ws2tcpip.h"

#define TARGET_IP	"127.0.0.1"

#define BUFFERS_LEN 1024
#define NAME_LEN 64



#define SENDER
//#define RECEIVER

#ifdef SENDER
#define TARGET_PORT 5555
#define LOCAL_PORT 8888
#endif // SENDER

#ifdef RECEIVER
#define TARGET_PORT 8888
#define LOCAL_PORT 5555
#endif // RECEIVER

typedef struct  {
	char fileName[NAME_LEN];
} PacketFileName;

typedef struct  {
	char data[BUFFERS_LEN];
	// _Bool is_last; ???
} PacketData;

typedef struct {
	short type;
	short crc;
	union {
		PacketFileName fileNamePacket;
		PacketData dataPacket;
	};
} Packet;

// typedef ACK packet

typedef enum {
	FILESIZE,
	DATA
} PacketType;

//typedef struct PacketData {
//	char data[BUFFERS_LEN];
//};

void InitWinsock()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
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

	char FILENAME[NAME_LEN] = "ReadMe.txt";

	// load the file to be sent
	FILE* file_in = fopen(FILENAME, "r");

	Packet fileNamePacket;
	Packet dataPacket;
	
	fileNamePacket.type = FILESIZE;
	strcpy(fileNamePacket.fileNamePacket.fileName, FILENAME);
	printf("sending file name packet: %s\n", fileNamePacket.fileNamePacket.fileName);
	sendto(socketS, (char*)fileNamePacket, sizeof(Packet), 0, (sockaddr*)&addrDest, sizeof(addrDest));


	//for (char c; fread(c, 1, 1, file_in) != EOF;) { // maybe read entire buffersize?
	//	if (i > BUFFERS_LEN) {
	//		// send packet and reset data
	//		i = 0;
	//	}
	//	packet_tx.data[i] == c;
	//	i++;
	//}
	//if (i != 0) {
	//	// send the remaining data
	//}
	
	strncpy(buffer_tx, "Hello world payload!\n", BUFFERS_LEN); //put some data to buffer
	printf("Sending packet.\n");
	sendto(socketS, buffer_tx, strlen(buffer_tx), 0, (sockaddr*)&addrDest, sizeof(addrDest));	

	closesocket(socketS);

#endif // SENDER

#ifdef RECEIVER


	_Bool continue_listening = true;
	stuPacket packet_rx;
	while (continue_listening) {
		if (recvfrom(socketS, buffer_rx, sizeof(buffer_rx), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR) {
			printf("Socket error!\n");
			getchar();
			return 1;
		}
		else
	
	}

	strncpy(buffer_rx, "No data received.\n", BUFFERS_LEN);
	printf("Waiting for datagram ...\n");
	if(recvfrom(socketS, buffer_rx, sizeof(buffer_rx), 0, (sockaddr*)&from, &fromlen) == SOCKET_ERROR){
		printf("Socket error!\n");
		getchar();
		return 1;
	}
	else
		printf("Datagram: %s", buffer_rx);

	closesocket(socketS);
#endif
	//**********************************************************************

	getchar(); //wait for press Enter
	return 0;
}
