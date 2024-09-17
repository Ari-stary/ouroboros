#include <iostream>
#include <Windows.h>
#include <shlobj.h>  // For SHGetFolderPath
#include <string>
#include <filesystem>

bool IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        if (!CheckTokenMembership(NULL, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }

    return isAdmin == TRUE;
}

void RelaunchAsAdmin() {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"\"C:\\PathToYourExecutable.exe\"";  // Replace with the path to your executable
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        std::cerr << "Failed to relaunch as admin." << std::endl;
        exit(1);
    }
}

void MoveToSystem32() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        std::cerr << "Failed to get executable path." << std::endl;
        return;
    }

    std::wstring sourcePath(exePath);
    std::wstring destPath(L"C:\\Windows\\System32\\");
    destPath += std::filesystem::path(sourcePath).filename().wstring();

    if (!MoveFileExW(sourcePath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::cerr << "Failed to move file to System32." << std::endl;
    }
    else {
        std::cout << "File successfully moved to System32." << std::endl;
    }
}

int main() {
    // First privilege check
    if (!IsAdmin()) {
        std::cout << "Requesting administrative privileges..." << std::endl;
        RelaunchAsAdmin();
        return 0;  // Exit the current process to relaunch it
    }

    // If already admin, perform the move operation
    std::cout << "Administrative privileges granted." << std::endl;
    MoveToSystem32();

    return 0;
}
