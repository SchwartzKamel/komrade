#pragma once
#ifndef MEMORY_H
#define MEMORY_H

#include <windows.h>
#include <vector>
#include <cstdint> // For uintptr_t
#include <string>  // For module name in constructor/attach

namespace Memory {

    // Converts IDA style pattern (e.g., "8B FF 55 8B EC ? ? ? ? 83") to bytes and mask
    bool PatternToBytes(const char* pattern, std::vector<int>& outBytes);

    // Scans a memory region for a byte pattern
    uintptr_t FindPattern(HANDLE processHandle, uintptr_t startAddress, size_t size, const char* pattern);

    // Scans a specific module within a process for a byte pattern
    uintptr_t FindPatternInModule(HANDLE processHandle, const wchar_t* moduleName, const char* pattern);

    // --- Basic Memory Operations ---

    // Reads memory from the target process
    // Returns true on success, false on failure
    bool ReadMemory(HANDLE processHandle, LPCVOID address, LPVOID buffer, SIZE_T size, SIZE_T* bytesRead = nullptr);

    // Writes memory to the target process
    // Returns true on success, false on failure
    bool WriteMemory(HANDLE processHandle, LPVOID address, LPCVOID buffer, SIZE_T size, SIZE_T* bytesWritten = nullptr);

    // --- Hooking Functions (using MinHook) ---

    // Initializes the MinHook library
    bool InitializeHooking();

    // Uninitializes the MinHook library
    void UninitializeHooking();

    // Creates and enables a detour hook
    // pTarget: Address of the function to hook
    // pDetour: Address of the detour function
    // ppOriginal: Pointer to receive the address of the original function (trampoline)
    bool CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);

    // Removes a hook previously created with CreateHook
    bool RemoveHook(LPVOID pTarget);

    // Enables a hook (required after CreateHook) - Note: CreateHook now enables by default for simplicity
    // bool EnableHook(LPVOID pTarget); // Kept for reference, but CreateHook enables

    // Disables a hook without removing it
    bool DisableHook(LPVOID pTarget);

    // --- Memory Manager Class ---

    class MemoryManager {
    private:
        HANDLE processHandle = NULL;
        DWORD processId = 0;
        HWND gameWindow = NULL; // Optional: Store window handle if needed

    public:
        MemoryManager();
        ~MemoryManager();

        // Attach to a process by window title or process ID
        bool Attach(const wchar_t* windowTitle, DWORD accessRights = PROCESS_ALL_ACCESS);
        bool Attach(DWORD procId, DWORD accessRights = PROCESS_ALL_ACCESS);
        void Detach();

        bool IsAttached() const;
        HANDLE GetProcessHandle() const;
        DWORD GetProcessId() const;

        // Wrapper methods for memory operations
        uintptr_t FindPattern(uintptr_t startAddress, size_t size, const char* pattern);
        uintptr_t FindPatternInModule(const wchar_t* moduleName, const char* pattern);
        bool ReadMemory(LPCVOID address, LPVOID buffer, SIZE_T size, SIZE_T* bytesRead = nullptr);
        bool WriteMemory(LPVOID address, LPCVOID buffer, SIZE_T size, SIZE_T* bytesWritten = nullptr);

        // Template versions for convenience
        template <typename T>
        T Read(LPCVOID address) {
            T value = {}; // Default initialize
            ReadMemory(address, &value, sizeof(T));
            return value;
        }

        template <typename T>
        bool Write(LPVOID address, const T& value) {
            return WriteMemory(address, &value, sizeof(T));
        }

        // Wrapper methods for hooking (optional, could be separate HookManager)
        // For now, keep them here for simplicity as per checklist item
        bool InitializeHooking(); // Calls the namespace function
        void UninitializeHooking(); // Calls the namespace function
        bool CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
        bool RemoveHook(LPVOID pTarget);
        bool DisableHook(LPVOID pTarget);
    };

} // namespace Memory

#endif // MEMORY_H