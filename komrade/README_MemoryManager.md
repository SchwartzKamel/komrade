# MemoryManager Subsystem Usage

This document describes how to use the `MemoryManager` class and related functions found in `memory.h` and `memory.cpp`.

## Overview

The `MemoryManager` class provides an interface for interacting with the memory of an external process, specifically designed for game hacking tasks like signature scanning and function hooking. It wraps Windows API calls and the MinHook library.

## Core Components

*   **`memory.h` / `memory.cpp`**: Contains the `Memory` namespace, including the `MemoryManager` class and standalone helper functions.
*   **`MemoryManager` Class**: The main class to instantiate for memory operations.
*   **MinHook**: External library required for hooking functionality. **Note:** You must download MinHook and configure the project's include and library paths manually.

## Basic Usage

```cpp
#include "memory.h"
#include <iostream>

int main() {
    Memory::MemoryManager mem;
    const wchar_t* targetProcess = L"ac_client.exe"; // Example: AssaultCube

    // 1. Attach to the target process (by window title or process ID)
    DWORD procId = GetProcID(targetProcess); // Assuming GetProcID helper exists
    if (!procId || !mem.Attach(procId)) {
        std::cerr << "Failed to attach to process: " << targetProcess << std::endl;
        return 1;
    }
    std::cout << "Attached to process ID: " << mem.GetProcessId() << std::endl;

    // --- Signature Scanning ---
    const char* somePattern = "8B 0D ? ? ? ? 85 C9"; // Example pattern
    uintptr_t foundAddr = mem.FindPatternInModule(targetProcess, somePattern);
    if (foundAddr) {
        std::cout << "Pattern found at: 0x" << std::hex << foundAddr << std::endl;
    } else {
        std::cout << "Pattern not found." << std::endl;
    }

    // --- Reading Memory ---
    uintptr_t someAddress = foundAddr + 2; // Example address
    int valueRead = mem.Read<int>((LPCVOID)someAddress);
    std::cout << "Value read from 0x" << std::hex << someAddress << ": " << std::dec << valueRead << std::endl;

    // --- Writing Memory ---
    int newValue = 1337;
    if (mem.Write<int>((LPVOID)someAddress, newValue)) {
        std::cout << "Wrote " << newValue << " to 0x" << std::hex << someAddress << std::endl;
    } else {
        std::cout << "Failed to write memory." << std::endl;
    }

    // --- Hooking (Requires MinHook Setup) ---
    /*
    // Example: Hooking a hypothetical function at a known address
    typedef void (*tMyFunction)();
    tMyFunction fpOriginalMyFunction = nullptr;
    void DetourMyFunction() {
        std::cout << "DetourMyFunction called!" << std::endl;
        fpOriginalMyFunction(); // Call original via trampoline
    }

    uintptr_t targetFuncAddr = 0xDEADBEEF; // Replace with actual address

    if (mem.InitializeHooking()) {
        if (mem.CreateHook((LPVOID)targetFuncAddr, &DetourMyFunction, (LPVOID*)&fpOriginalMyFunction)) {
            std::cout << "Hook created for function at 0x" << std::hex << targetFuncAddr << std::endl;
            // ... function is now hooked ...

            // Cleanup:
            mem.RemoveHook((LPVOID)targetFuncAddr);
        }
        mem.UninitializeHooking();
    }
    */

    // 2. Detach when done
    mem.Detach();
    std::cout << "Detached from process." << std::endl;

    return 0;
}
```

## Building

1.  **Download MinHook:** Obtain the MinHook library (header and `.lib` file for your target architecture - likely x86 for AssaultCube).
2.  **Configure Visual Studio:**
    *   Add the path to `MinHook.h` to the project's "Additional Include Directories".
    *   Add the path to the MinHook `.lib` file to the project's "Additional Library Directories".
    *   Add the MinHook `.lib` filename (e.g., `libMinHook.x86.lib`) to the project's "Additional Dependencies" under the Linker settings.
3.  **Compile:** Build the project.

## Notes

*   Error handling in the examples is minimal. Add more robust checks as needed.
*   Ensure you have the correct permissions to open and manipulate the target process memory. Run as administrator if necessary.
*   Memory addresses and signatures are specific to the game version and may change with updates.