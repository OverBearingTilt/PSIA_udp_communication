#include "stdafx.h"
#include "utils.h"

void InitWinsock() {
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
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

//convert wide string to UTF-8 string
std::string WideToUtf8(const std::wstring& wideStr) {
	if (wideStr.empty()) return {};

	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (size_needed <= 0) return {};

	std::string utf8Str(size_needed - 1, 0); // exclude null terminator
	WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], size_needed, nullptr, nullptr);
	return utf8Str;
}

//file selection function
bool OpenFileDialog(std::string& filePath, std::string& fileName) {
	wchar_t filePathW[MAX_PATH] = L"";

	OPENFILENAMEW ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = L"All Files\0*.*\0";
	ofn.lpstrFile = filePathW;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

	if (GetOpenFileNameW(&ofn)) {
		std::wstring widePath(filePathW);
		filePath = WideToUtf8(widePath);

		wchar_t* fileNameW = PathFindFileNameW(filePathW);
		fileName = WideToUtf8(fileNameW);
		return true;
	}
	else {
		return false;
	}
}


void reset_data(char data[BUFFERS_LEN]) {
	for (int i = 0; i < BUFFERS_LEN; i++) {
		data[i] = 0;
	}
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

std::string toLowerCase(const std::string& input) {
	std::string result = input;
	std::transform(result.begin(), result.end(), result.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return result;
}