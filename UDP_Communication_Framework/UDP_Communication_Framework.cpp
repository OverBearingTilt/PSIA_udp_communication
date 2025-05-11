// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
//

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include "Receiver.h"
#include "Sender.h"
#include "utils.h"

#define TARGET_IP "192.168.0.101"
// 147.32.216.175

#define BUFFERS_LEN 1024 - sizeof(uint32_t) - 2*sizeof(int) - sizeof(char)
#define NAME_LEN 64
#define SHA256_LEN 64
#define TOLERANCE 100
#define TIMEOUT_SEC 1

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shlwapi.lib") //for PathFindFileName

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define RESET "\x1b[0m"

#define SENDER
//#define RECEIVER

#ifdef SENDER
#define TARGET_PORT 5001
#define LOCAL_PORT 5000
#endif // SENDER

#ifdef RECEIVER
#define TARGET_PORT 5000
#define LOCAL_PORT 5001
#endif // RECEIVER


int main(int argc, char* argv[])
{
	int retValue = -1;


#ifdef SENDER
	try {
		// Define the local and target ports and the target IP address
		int localPort = 5000;  // Port to send from
		int targetPort = 5001; // Port to send to
		wchar_t targetIP[] = _T(TARGET_IP); // Target IP address

		// File path and name
		std::string strFilePath;
		std::string strFileName;

		// Open file dialog to select the file
		if (OpenFileDialog(strFilePath, strFileName)) {
			std::cout << "Full path: " << strFilePath << std::endl;
			std::cout << "File name: " << strFileName << std::endl;
		}
		else {
			std::cerr << "File selection cancelled or failed." << std::endl;
			return -1;
		}

		// Create a Sender object
		Sender sender(localPort, targetPort, targetIP);

		// Run the sender
		int result = sender.run(strFilePath, strFileName);

		// Check the result of the transfer
		if (result == 0) {
			std::cout << "File transfer completed successfully." << std::endl;
		}
		else {
			std::cerr << "File transfer failed." << std::endl;
		}

		return result;
	}
	catch (const std::exception& e) {
		std::cerr << "An error occurred: " << e.what() << std::endl;
		return 1;
	}
#endif // SENDER

#ifdef RECEIVER
	try {
		// Define the local and target ports and the target IP address
		int localPort = 5001;  // Port to listen on
		int targetPort = 5000; // Port to send responses to
		wchar_t targetIP[] = _T(TARGET_IP); // Target IP address

		// Create a Receiver object
		Receiver receiver(localPort, targetPort, targetIP);

		// Run the receiver
		int result = receiver.run();

		// Check the result of the transfer
		if (result == 0) {
			std::cout << "File transfer completed successfully." << std::endl;
		}
		else {
			std::cerr << "File transfer failed." << std::endl;
		}

		return result;
	}
	catch (const std::exception& e) {
		std::cerr << "An error occurred: " << e.what() << std::endl;
		return 1;
	}
#endif


	getchar(); //wait for press Enter
	return retValue;
}
