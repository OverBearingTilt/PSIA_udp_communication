// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
//

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include "Receiver.h"
#include "Sender.h"
#include "utils.h"

#define TARGET_IP "192.168.0.105"

#define BUFFERS_LEN 1024 - sizeof(uint32_t) - 2*sizeof(int) - sizeof(char)
#define NAME_LEN 64
#define SHA256_LEN 64
#define TOLERANCE 100

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shlwapi.lib") //for PathFindFileName

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define RESET "\x1b[0m"

//#define SENDER
#define RECEIVER

#define PORT_A 5000
#define PORT_B 5001
// sender with netderper:
// local/A = 5004
// target/B = 5002

int main(int argc, char* argv[])
{
	int retValue = -1;


#ifdef SENDER
	try {
		// Define the local and target ports and the target IP address
		int localPort = PORT_A;  // Port to send from
		int targetPort = PORT_B; // Port to send to
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
			std::cout << GREEN << "File transfer completed successfully."<< RESET << std::endl;
		}
		else {
			std::cerr<< RED << "File transfer failed." << RESET << std::endl;
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
		int localPort = PORT_B;  // Port to listen on
		int targetPort = PORT_A; // Port to send responses to
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
