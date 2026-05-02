#include "memory.h"
#include <windows.h>
#include <vector>
#include <sstream>
#include <string>
#include <Psapi.h> // For GetModuleInformation
#include <iostream> // For potential debugging
#include "MinHook.h" // Include MinHook header

namespace Memory {

    // Converts IDA style pattern (e.g., "8B FF 55 8B EC ? ? ? ? 83") to bytes and mask
    // Output: vector of integers. -1 represents a wildcard byte.
    bool PatternToBytes(const char* pattern, std::vector<int>& outBytes) {
        outBytes.clear();
        std::stringstream ss(pattern);
        std::string byteStr;

        while (ss >> byteStr) {
            if (byteStr == "?" || byteStr == "??") {
                outBytes.push_back(-1); // Wildcard
            } else {
                try {
                    int byteVal = std::stoi(byteStr, nullptr, 16);
                    if (byteVal >= 0 && byteVal <= 255) {
                        outBytes.push_back(byteVal);
                    } else {
                        // Invalid byte value
                        return false;
                    }
                } catch (const std::invalid_argument&) {
                    // Conversion failed (not a hex byte or wildcard)
                    return false;
                } catch (const std::out_of_range&) {
                    // Conversion failed (out of range for int)
                    return false;
                }
            }
        }
        return !outBytes.empty();
    }

    // Scans a memory region for a byte pattern
    uintptr_t FindPattern(HANDLE processHandle, uintptr_t startAddress, size_t size, const char* pattern) {
        std::vector<int> patternBytes;
        if (!PatternToBytes(pattern, patternBytes) || patternBytes.empty()) {
            return 0; // Invalid pattern
        }

        size_t patternSize = patternBytes.size();
        const size_t chunkSize = 4096; // Read memory in chunks
        std::vector<byte> chunkBuffer(chunkSize);

        for (uintptr_t currentOffset = 0; currentOffset < size - patternSize; ) {
            SIZE_T bytesRead = 0;
            uintptr_t readAddress = startAddress + currentOffset;
            size_t sizeToRead = min(chunkSize, size - currentOffset); // Don't read past the end

            if (!ReadProcessMemory(processHandle, (LPCVOID)readAddress, chunkBuffer.data(), sizeToRead, &bytesRead) || bytesRead == 0) {
                 // Failed to read or reached end unexpectedly
                 // std::cerr << "ReadProcessMemory failed or read 0 bytes at " << std::hex << readAddress << std::endl;
                 break; // Stop scanning if read fails
            }

            // Adjust sizeToRead to actual bytes read, important for the last chunk
            sizeToRead = bytesRead;

            // Scan within the chunk
            for (size_t chunkOffset = 0; chunkOffset < sizeToRead - patternSize; ++chunkOffset) {
                bool found = true;
                for (size_t patternIndex = 0; patternIndex < patternSize; ++patternIndex) {
                    if (patternBytes[patternIndex] != -1 && // If not a wildcard
                        patternBytes[patternIndex] != chunkBuffer[chunkOffset + patternIndex]) {
                        found = false;
                        break;
                    }
                }

                if (found) {
                    return readAddress + chunkOffset; // Found the pattern!
                }
            }

            // Move to the next chunk, accounting for potential overlap
            // Move forward by chunk size minus pattern size to catch patterns spanning chunks
            currentOffset += (sizeToRead > patternSize) ? (sizeToRead - patternSize) : 1;
             // Ensure progress even if chunk is smaller than pattern
             if (sizeToRead <= patternSize) {
                 currentOffset++;
             }
        }

        return 0; // Pattern not found
    }

    // Scans a specific module within a process for a byte pattern
    uintptr_t FindPatternInModule(HANDLE processHandle, const wchar_t* moduleName, const char* pattern) {
        HMODULE hMods[1024];
        DWORD cbNeeded;
        MODULEINFO modInfo = { 0 }; // Initialize MODULEINFO structure

        // Enumerate process modules
        if (EnumProcessModules(processHandle, hMods, sizeof(hMods), &cbNeeded)) {
            for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
                wchar_t szModName[MAX_PATH];

                // Get the full path to the module's file.
                if (GetModuleFileNameExW(processHandle, hMods[i], szModName, sizeof(szModName) / sizeof(wchar_t))) {
                    // Extract module name from path
                    wchar_t* baseName = wcsrchr(szModName, L'\\');
                    if (baseName == nullptr) {
                        baseName = szModName; // No backslash, use the whole string
                    } else {
                        baseName++; // Move past the backslash
                    }

                    // Compare module name (case-insensitive)
                    if (_wcsicmp(baseName, moduleName) == 0) {
                        // Found the module, get its information
                        if (GetModuleInformation(processHandle, hMods[i], &modInfo, sizeof(modInfo))) {
                            // Scan within this module's memory range
                            return FindPattern(processHandle, (uintptr_t)modInfo.lpBaseOfDll, modInfo.SizeOfImage, pattern);
                        }
                        break; // Stop searching once module info is retrieved (or fails)
                    }
                }
            }
        } else {
             // std::cerr << "EnumProcessModules failed: " << GetLastError() << std::endl;
        }

        // Module not found or error occurred
        return 0;
    }

        // Module not found or error occurred
        return 0;
    }

    // --- Basic Memory Operations ---

    // Reads memory from the target process
    bool ReadMemory(HANDLE processHandle, LPCVOID address, LPVOID buffer, SIZE_T size, SIZE_T* bytesRead) {
        return ReadProcessMemory(processHandle, address, buffer, size, bytesRead);
    }

    // Writes memory to the target process
    bool WriteMemory(HANDLE processHandle, LPVOID address, LPCVOID buffer, SIZE_T size, SIZE_T* bytesWritten) {
        DWORD oldProtect;
        // Temporarily change protection to allow writing
        if (!VirtualProtectEx(processHandle, address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            // std::cerr << "VirtualProtectEx (write enable) failed: " << GetLastError() << std::endl;
            return false;
        }

        bool result = WriteProcessMemory(processHandle, address, buffer, size, bytesWritten);
        if (!result) {
             // std::cerr << "WriteProcessMemory failed: " << GetLastError() << std::endl;
        }

        // Restore original protection
        DWORD tempProtect; // Need a variable for the function call, even if we don't use the result here
        if (!VirtualProtectEx(processHandle, address, size, oldProtect, &tempProtect)) {
             // std::cerr << "VirtualProtectEx (restore) failed: " << GetLastError() << std::endl;
             // Write succeeded, but restoring protection failed. Still return true? Or false?
             // For now, return the result of the WriteProcessMemory call.
        }

        return result;
    }


    // --- Hooking Functions (using MinHook) ---

    // Initializes the MinHook library
    bool InitializeHooking() {
        if (MH_Initialize() != MH_OK) {
            // std::cerr << "Failed to initialize MinHook." << std::endl;
            return false;
        }
        // std::cout << "MinHook initialized successfully." << std::endl;
        return true;
    }

    // Uninitializes the MinHook library
    void UninitializeHooking() {
        MH_Uninitialize();
        // std::cout << "MinHook uninitialized." << std::endl;
    }

    // Creates and enables a detour hook
    bool CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal) {
        // Create the hook but don't enable it yet.
        MH_STATUS status = MH_CreateHook(pTarget, pDetour, ppOriginal);
        if (status != MH_OK) {
            // std::cerr << "MH_CreateHook failed: " << MH_StatusToString(status) << std::endl;
            return false;
        }

        // Enable the hook.
        status = MH_EnableHook(pTarget);
        if (status != MH_OK) {
            // std::cerr << "MH_EnableHook failed: " << MH_StatusToString(status) << std::endl;
            // Attempt to remove the hook if enabling failed
            MH_RemoveHook(pTarget);
            return false;
        }

        // std::cout << "Hook created and enabled for target: " << pTarget << std::endl;
        return true;
    }

    // Removes a hook previously created with CreateHook
    bool RemoveHook(LPVOID pTarget) {
        // Disable the hook first (optional but good practice)
        MH_DisableHook(pTarget); // Ignore result, just try to disable

        // Remove the hook
        MH_STATUS status = MH_RemoveHook(pTarget);
        if (status != MH_OK) {
            // std::cerr << "MH_RemoveHook failed: " << MH_StatusToString(status) << std::endl;
            // Re-enable if removal failed? Depends on desired behavior.
            // MH_EnableHook(pTarget);
            return false;
        }
        // std::cout << "Hook removed for target: " << pTarget << std::endl;
        return true;
    }

    // Disables a hook without removing it
    bool DisableHook(LPVOID pTarget) {
         MH_STATUS status = MH_DisableHook(pTarget);
         if (status != MH_OK) {
            // std::cerr << "MH_DisableHook failed: " << MH_StatusToString(status) << std::endl;
            return false;
         }
         // std::cout << "Hook disabled for target: " << pTarget << std::endl;
         return true;
    }


// --- Memory Manager Class Implementation ---

    MemoryManager::MemoryManager() : processHandle(NULL), processId(0), gameWindow(NULL) {}

    MemoryManager::~MemoryManager() {
        Detach(); // Ensure resources are released
    }

    bool MemoryManager::Attach(const wchar_t* windowTitle, DWORD accessRights) {
        Detach(); // Detach if already attached

        gameWindow = FindWindowW(NULL, windowTitle);
        if (gameWindow == NULL) {
            // std::cerr << "FindWindowW failed: " << GetLastError() << std::endl;
            return false;
        }

        GetWindowThreadProcessId(gameWindow, &amp;processId);
        if (processId == 0) {
            // std::cerr << "GetWindowThreadProcessId failed: " << GetLastError() << std::endl;
            return false;
        }

        processHandle = OpenProcess(accessRights, FALSE, processId);
        if (processHandle == NULL) {
            // std::cerr << "OpenProcess failed: " << GetLastError() << std::endl;
            processId = 0;
            gameWindow = NULL;
            return false;
        }

        // std::cout << "Attached to process ID: " << processId << std::endl;
        return true;
    }

    bool MemoryManager::Attach(DWORD procId, DWORD accessRights) {
        Detach(); // Detach if already attached

        processHandle = OpenProcess(accessRights, FALSE, procId);
        if (processHandle == NULL) {
            // std::cerr << "OpenProcess failed: " << GetLastError() << std::endl;
            return false;
        }

        processId = procId;
        gameWindow = NULL; // Window handle is not available in this case

        // std::cout << "Attached to process ID: " << processId << std::endl;
        return true;
    }

    void MemoryManager::Detach() {
        if (processHandle != NULL) {
            CloseHandle(processHandle);
            processHandle = NULL;
            processId = 0;
            gameWindow = NULL;
            // std::cout << "Detached from process." << std::endl;
        }
    }

    bool MemoryManager::IsAttached() const {
        return processHandle != NULL &amp;&amp; processId != 0;
    }

    HANDLE MemoryManager::GetProcessHandle() const {
        return processHandle;
    }

     DWORD MemoryManager::GetProcessId() const {
        return processId;
    }

    // --- Wrapper Methods ---

    uintptr_t MemoryManager::FindPattern(uintptr_t startAddress, size_t size, const char* pattern) {
        if (!IsAttached()) return 0;
        return Memory::FindPattern(processHandle, startAddress, size, pattern);
    }

    uintptr_t MemoryManager::FindPatternInModule(const wchar_t* moduleName, const char* pattern) {
        if (!IsAttached()) return 0;
        return Memory::FindPatternInModule(processHandle, moduleName, pattern);
    }

    bool MemoryManager::ReadMemory(LPCVOID address, LPVOID buffer, SIZE_T size, SIZE_T* bytesRead) {
        if (!IsAttached()) return false;
        return Memory::ReadMemory(processHandle, address, buffer, size, bytesRead);
    }

    bool MemoryManager::WriteMemory(LPVOID address, LPCVOID buffer, SIZE_T size, SIZE_T* bytesWritten) {
        if (!IsAttached()) return false;
        return Memory::WriteMemory(processHandle, address, buffer, size, bytesWritten);
    }

    // Hooking wrappers - directly call the namespace functions
    bool MemoryManager::InitializeHooking() {
        // Note: MinHook initialization is global, not per-manager instance
        return Memory::InitializeHooking();
    }

    void MemoryManager::UninitializeHooking() {
        // Note: MinHook uninitialization is global
        Memory::UninitializeHooking();
    }

    bool MemoryManager::CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal) {
        // Hooking itself doesn't strictly require the manager's process handle
        // once initialized, but keeping it here for encapsulation consistency.
        if (!IsAttached()) return false; // Or maybe allow even if not attached? Decide based on use case.
                                        // For now, require attachment.
        return Memory::CreateHook(pTarget, pDetour, ppOriginal);
    }

    bool MemoryManager::RemoveHook(LPVOID pTarget) {
        if (!IsAttached()) return false; // Require attachment for consistency
        return Memory::RemoveHook(pTarget);
    }

     bool MemoryManager::DisableHook(LPVOID pTarget) {
        if (!IsAttached()) return false; // Require attachment for consistency
        return Memory::DisableHook(pTarget);
    }
} // namespace Memory