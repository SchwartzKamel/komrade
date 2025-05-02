// HackAnyGame.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "stdafx.h"
#include "proc.h" // Keep for GetProcID, GetModuleBaseAddress if needed elsewhere, though MemoryManager duplicates some functionality
#include "mem.h"  // Keep for PatchEx/NopEx used in recoil_delete
#include "memory.h" // Include the new memory subsystem header

// --- Old Example (Commented Out) ---
/*
static int leet_ammo()
{
	//Get ProcID of the target process
	DWORD procID = GetProcID(L"ac_client.exe");

	//Getmodulebaseaddress
	uintptr_t moduleBase = GetModuleBaseAddress(procID, L"ac_client.exe");

	//Get Handle to Process
	HANDLE hProcess = 0;
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, procID);

	//Resolve base address of the pointer chain
	uintptr_t dynamicPtrBaseAddr = moduleBase + 0x10f4f4; // Old offset, might be incorrect for v1.2.0.2

	std::cout << "DynamicPtrBaseAddr = " << "0x" << std::hex << dynamicPtrBaseAddr << std::endl;

	//Resolve our ammo pointer chain
	std::vector<unsigned int> ammoOffsets = { 0x374, 0x14, 0x0 }; // Old offsets
	uintptr_t ammoAddr = FindDMAAddy(hProcess, dynamicPtrBaseAddr, ammoOffsets);

	std::cout << "ammoAddr = " << "0x" << std::hex << ammoAddr << std::endl;

	//Read Ammo value
	int ammoValue = 0;

	ReadProcessMemory(hProcess, (BYTE*)ammoAddr, &ammoValue, sizeof(ammoValue), nullptr);
	std::cout << "Current ammo = " << std::dec << ammoValue << std::endl;

	//Write to it
	int newAmmo = 1337;
	WriteProcessMemory(hProcess, (BYTE*)ammoAddr, &newAmmo, sizeof(newAmmo), nullptr);

	//Read out again
	ReadProcessMemory(hProcess, (BYTE*)ammoAddr, &ammoValue, sizeof(ammoValue), nullptr);

	std::cout << "New ammo = " << std::dec << ammoValue << std::endl;


	getchar();
	return 0;
}
*/

// --- Existing Cheat Loop ---
{
	//Get ProcID of the target process
	DWORD procID = GetProcID(L"ac_client.exe");

	//Getmodulebaseaddress
	uintptr_t moduleBase = GetModuleBaseAddress(procID, L"ac_client.exe");

	//Get Handle to Process
	HANDLE hProcess = 0;
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, procID);

	//Resolve base address of the pointer chain
	uintptr_t dynamicPtrBaseAddr = moduleBase + 0x10f4f4;

	std::cout << "DynamicPtrBaseAddr = " << "0x" << std::hex << dynamicPtrBaseAddr << std::endl;

	//Resolve our ammo pointer chain
	std::vector<unsigned int> ammoOffsets = { 0x374, 0x14, 0x0 };
	uintptr_t ammoAddr = FindDMAAddy(hProcess, dynamicPtrBaseAddr, ammoOffsets);

	std::cout << "ammoAddr = " << "0x" << std::hex << ammoAddr << std::endl;

	//Read Ammo value
	int ammoValue = 0;

	ReadProcessMemory(hProcess, (BYTE*)ammoAddr, &ammoValue, sizeof(ammoValue), nullptr);
	std::cout << "Current ammo = " << std::dec << ammoValue << std::endl;

	//Write to it
	int newAmmo = 1337;
	WriteProcessMemory(hProcess, (BYTE*)ammoAddr, &newAmmo, sizeof(newAmmo), nullptr);

	//Read out again
	ReadProcessMemory(hProcess, (BYTE*)ammoAddr, &ammoValue, sizeof(ammoValue), nullptr);

	std::cout << "New ammo = " << std::dec << ammoValue << std::endl;


	getchar();
	return 0;
}

static int recoil_delete()
{
	HANDLE hProcess = 0;

	uintptr_t moduleBase = 0, localPlayerPtr = 0, healthAddr = 0, armorAddr = 0;
	uintptr_t rifleAmmoAddr = 0, smgAmmoAddr = 0, sniperAmmoAddr = 0, shotgunAmmoAddr = 0, pistolAmmoAddr = 0, grenadeAmmoAddr = 0;
	uintptr_t fastRifleAddr = 0, fastSniperAddr = 0, fastShotgunAddr = 0;
	uintptr_t autoShootAddr = 0;
	uintptr_t fovAddr = 0;
	bool bHealth = false, bArmor = false, bAmmo = false, bRecoil = false, bFastFire = false, bAutoShoot = false, bFovChange = false; // Added bFovChange

	const int freezeValue = 1337; // Value to freeze health/armor at
	const int ammoFreezeValue = 999; // Value to freeze ammo at
	const int fastFireValue = 0; // Value to write for fast fire (usually 0)
	const int autoShootValueOn = 1; // Value to write for auto shoot ON
	const int autoShootValueOff = 0; // Value to write for auto shoot OFF
	const float newFovValue = 110.0f; // Desired FOV value
	const float defaultFovValue = 90.0f; // Default FOV value (adjust if needed)

	DWORD procID = GetProcID(L"ac_client.exe");

	if (procID)
	{
		hProcess = OpenProcess(PROCESS_ALL_ACCESS, NULL, procID);

		moduleBase = GetModuleBaseAddress(procID, L"ac_client.exe");

		// Updated LocalPlayer offset
		localPlayerPtr = moduleBase + 0x0017E0A8;

		// Get addresses for health and armor using new offsets
		// Note: These are direct offsets from the LocalPlayer pointer, not multi-level pointers in this case.
		healthAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0xEC });
		armorAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0xF0 });

		// Get ammo addresses using new offsets
		rifleAmmoAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0x140 });
		smgAmmoAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0x138 });
		sniperAmmoAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0x13C });
		shotgunAmmoAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0x134 });
		pistolAmmoAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0x12C });
		grenadeAmmoAddr = FindDMAAddy(hProcess, localPlayerPtr, { 0x144 });
	}
	
	else
	{
		std::cout << "Process not found, press enter to exit\n";
		getchar();
		return 0;
	}

	DWORD dwExit = 0;

	while (GetExitCodeProcess(hProcess, &dwExit) && dwExit == STILL_ACTIVE)
	{
		if (GetAsyncKeyState(VK_F1) & 1)
		{
			bHealth = !bHealth;
			std::cout << "Health Freeze: " << (bHealth ? "ON" : "OFF") << std::endl;
		}
		if (GetAsyncKeyState(VK_F5) & 1) // Added hotkey for Armor
		{
			bArmor = !bArmor;
			std::cout << "Armor Freeze: " << (bArmor ? "ON" : "OFF") << std::endl;
		}
		if (GetAsyncKeyState(VK_F2) & 1)
		{
			bAmmo = !bAmmo;
			std::cout << "Ammo Freeze: " << (bAmmo ? "ON" : "OFF") << std::endl;
			// Removed old patching logic, now just toggles the freeze
		}
		if (GetAsyncKeyState(VK_F3) & 1)
		{
			bRecoil = !bRecoil;

			if (bRecoil)
			{
				mem::NopEx((BYTE*)(moduleBase + 0x63786), 10, hProcess);
			}
			else
			{
				mem::PatchEx((BYTE*)(moduleBase + 0x63786), (BYTE*)"\x50\x8D\x4C\x24\x1C\x8B\xCE\xFF\xD2", 10, hProcess);
			}
		}
		if (GetAsyncKeyState(VK_F6) & 1) // Added hotkey for Fast Fire
		{
			bFastFire = !bFastFire;
			std::cout << "Fast Fire: " << (bFastFire ? "ON" : "OFF") << std::endl;
		}
		if (GetAsyncKeyState(VK_F7) & 1) // Added hotkey for Auto Shoot
		{
			bAutoShoot = !bAutoShoot;
			std::cout << "Auto Shoot: " << (bAutoShoot ? "ON" : "OFF") << std::endl;
		}
		if (GetAsyncKeyState(VK_F8) & 1) // Added hotkey for FOV Change
		{
			bFovChange = !bFovChange;
			std::cout << "FOV Change: " << (bFovChange ? "ON (110)" : "OFF (Default)") << std::endl;
		}
		if (GetAsyncKeyState(VK_F12) & 1)
		{
			return 0;
		}

		// continuous write or freeze
		if (bHealth)
		{
			mem::PatchEx((BYTE*)healthAddr, (BYTE*)&freezeValue, sizeof(freezeValue), hProcess);
		}
		if (bArmor) // Added armor freeze logic
		{
			mem::PatchEx((BYTE*)armorAddr, (BYTE*)&freezeValue, sizeof(freezeValue), hProcess);
		}

		// Added ammo freeze logic
		if (bAmmo)
		{
			mem::PatchEx((BYTE*)rifleAmmoAddr, (BYTE*)&ammoFreezeValue, sizeof(ammoFreezeValue), hProcess);
			mem::PatchEx((BYTE*)smgAmmoAddr, (BYTE*)&ammoFreezeValue, sizeof(ammoFreezeValue), hProcess);
			mem::PatchEx((BYTE*)sniperAmmoAddr, (BYTE*)&ammoFreezeValue, sizeof(ammoFreezeValue), hProcess);
			mem::PatchEx((BYTE*)shotgunAmmoAddr, (BYTE*)&ammoFreezeValue, sizeof(ammoFreezeValue), hProcess);
			mem::PatchEx((BYTE*)pistolAmmoAddr, (BYTE*)&ammoFreezeValue, sizeof(ammoFreezeValue), hProcess);
			mem::PatchEx((BYTE*)grenadeAmmoAddr, (BYTE*)&ammoFreezeValue, sizeof(ammoFreezeValue), hProcess);
		}

		// Added fast fire logic
		if (bFastFire)
		{
			mem::PatchEx((BYTE*)fastRifleAddr, (BYTE*)&fastFireValue, sizeof(fastFireValue), hProcess);
			mem::PatchEx((BYTE*)fastSniperAddr, (BYTE*)&fastFireValue, sizeof(fastFireValue), hProcess);
			mem::PatchEx((BYTE*)fastShotgunAddr, (BYTE*)&fastFireValue, sizeof(fastFireValue), hProcess);
		}
		// Note: To properly disable fast fire, we might need to write the default value back (e.g., 1)
		// For simplicity, this version only enables it by writing 0.

		// Added auto shoot logic
		if (autoShootAddr) // Check if address is valid before writing
		{
			if (bAutoShoot)
			{
				mem::PatchEx((BYTE*)autoShootAddr, (BYTE*)&autoShootValueOn, sizeof(autoShootValueOn), hProcess);
			}
			else
			{
				// Write the OFF value when disabled to ensure it stops
				mem::PatchEx((BYTE*)autoShootAddr, (BYTE*)&autoShootValueOff, sizeof(autoShootValueOff), hProcess);
			}
		}

		// Added FOV change logic
		if (fovAddr) // Check if address is valid
		{
			if (bFovChange)
			{
				mem::PatchEx((BYTE*)fovAddr, (BYTE*)&newFovValue, sizeof(newFovValue), hProcess);
			}
			else
			{
				// Restore default FOV when disabled
				mem::PatchEx((BYTE*)fovAddr, (BYTE*)&defaultFovValue, sizeof(defaultFovValue), hProcess);
			}
		}


		Sleep(10);
	}

	std::cout << "Process not found, press enter to exit\n";
	getchar();
	return 0;
}


// --- Hooking Demonstration ---

// Define a function pointer type for the original function
typedef int(__stdcall* tOriginalFunction)(int, int);
// Pointer to hold the trampoline address to the original function
tOriginalFunction fpOriginalFunction = nullptr;

// The function we want to hook
int __stdcall OriginalFunction(int a, int b) {
	std::cout << "  OriginalFunction(" << a << ", " << b << ") called. Returning " << (a + b) << "." << std::endl;
	return a + b;
}

// Our detour function that will replace OriginalFunction
int __stdcall DetourFunction(int a, int b) {
	std::cout << ">> DetourFunction(" << a << ", " << b << ") called." << std::endl;
	// Call the original function using the trampoline
	int originalResult = fpOriginalFunction(a, b);
	std::cout << ">> Original function returned: " << originalResult << std::endl;
	int detourResult = a * b; // Do something different
	std::cout << ">> Detour returning: " << detourResult << std::endl;
	return detourResult;
}

// Function to demonstrate hooking
static int test_hooking() {
	Memory::MemoryManager memManager; // Using MemoryManager for consistency, though not strictly needed for local hook
	bool hookInitialized = false;
	bool hookCreated = false;

	std::cout << "\n--- Testing Hooking ---" << std::endl;

	// Initialize MinHook
	if (memManager.InitializeHooking()) {
		std::cout << "MinHook initialized successfully." << std::endl;
		hookInitialized = true;
	} else {
		std::cerr << "Error: Failed to initialize MinHook." << std::endl;
		goto cleanup; // Use goto for simple cleanup in this example
	}

	// Create the hook for OriginalFunction
	std::cout << "Creating hook for OriginalFunction at 0x" << std::hex << (uintptr_t)OriginalFunction << std::endl;
	if (memManager.CreateHook(&OriginalFunction, &DetourFunction, reinterpret_cast<LPVOID*>(&fpOriginalFunction))) {
		std::cout << "Hook created successfully. Trampoline at 0x" << std::hex << (uintptr_t)fpOriginalFunction << std::endl;
		hookCreated = true;
	} else {
		std::cerr << "Error: Failed to create hook." << std::endl;
		goto cleanup;
	}

	// Test the hook
	std::cout << "\nCalling OriginalFunction (should be detoured):" << std::endl;
	int result1 = OriginalFunction(5, 3);
	std::cout << "Result after hook: " << result1 << " (Expected detour result: 15)" << std::endl;

	// Disable the hook
	std::cout << "\nDisabling hook..." << std::endl;
	if (!memManager.DisableHook(&OriginalFunction)) {
		std::cerr << "Warning: Failed to disable hook." << std::endl;
	}

	// Test again (should call original directly)
	std::cout << "\nCalling OriginalFunction (hook disabled):" << std::endl;
	int result2 = OriginalFunction(5, 3);
	std::cout << "Result after disabling hook: " << result2 << " (Expected original result: 8)" << std::endl;

	// Re-enable hook (using CreateHook again implicitly enables if already created but disabled)
	// Or use MH_EnableHook directly if needed. For simplicity, we'll just remove it now.

cleanup:
	// Remove the hook if it was created
	if (hookCreated) {
		std::cout << "\nRemoving hook..." << std::endl;
		if (!memManager.RemoveHook(&OriginalFunction)) {
			std::cerr << "Warning: Failed to remove hook." << std::endl;
		}
	}

	// Uninitialize MinHook if it was initialized
	if (hookInitialized) {
		std::cout << "Uninitializing MinHook..." << std::endl;
		memManager.UninitializeHooking();
	}

	std::cout << "\n--- Hooking Test Complete ---" << std::endl;
	std::cout << "Press Enter to exit." << std::endl;
	getchar();
	return 0;
}


// --- Test Function for Signature Scanning ---
static int test_signature_scan()
{
	const wchar_t* processName = L"ac_client.exe";
	// Signature for player base pointer in Assault Cube v1.2.0.2
	// Found via Cheat Engine searching for value pointed to by ac_client.exe+17E0A8
	// Example Signature: Points to the instruction that loads the player base address
	const char* playerBaseSignature = "8B 0D ? ? ? ? 85 C9 74 0A 8B 01 FF 50 04"; // Example, verify this pattern

	Memory::MemoryManager memManager;

	std::cout << "Attempting to attach to process: " << processName << std::endl;

	// Attach using process name (window title might be less reliable)
	DWORD procId = GetProcID(processName); // Use old proc helper to get ID
	if (!procId) {
		std::cerr << "Error: Could not find process ID for " << processName << ". Is the game running?" << std::endl;
		getchar();
		return 1;
	}

	if (!memManager.Attach(procId)) {
		std::cerr << "Error: Failed to attach MemoryManager to process ID " << procId << "." << std::endl;
		getchar();
		return 1;
	}

	std::cout << "Successfully attached to process ID: " << memManager.GetProcessId() << std::endl;
	std::cout << "Scanning for player base signature in module: " << processName << std::endl;
	std::cout << "Pattern: " << playerBaseSignature << std::endl;

	uintptr_t foundAddress = memManager.FindPatternInModule(processName, playerBaseSignature);

	if (foundAddress != 0) {
		std::cout << "Signature found at address: 0x" << std::hex << foundAddress << std::endl;

		// Optional: Read the address the instruction points to (requires parsing the instruction)
        // For "8B 0D [addr]", the address is at foundAddress + 2
        uintptr_t pointerAddressLocation = foundAddress + 2;
        uintptr_t playerBaseAddressPtr = 0;
        if (memManager.ReadMemory((LPCVOID)pointerAddressLocation, &playerBaseAddressPtr, sizeof(playerBaseAddressPtr))) {
             std::cout << "Instruction points to address: 0x" << std::hex << playerBaseAddressPtr << std::endl;
             // Now you could read the actual player base:
             // uintptr_t playerBase = memManager.Read<uintptr_t>((LPCVOID)playerBaseAddressPtr);
             // std::cout << "Actual Player Base Address (read from pointer): 0x" << std::hex << playerBase << std::endl;
        } else {
            std::cerr << "Warning: Found signature but failed to read the pointer address from instruction." << std::endl;
        }

	} else {
		std::cerr << "Error: Signature pattern not found." << std::endl;
	}

	memManager.Detach();
	std::cout << "Detached from process." << std::endl;
	std::cout << "Press Enter to exit." << std::endl;
	getchar();
	return 0;
}


int main()
{
	// recoil_delete();       // Call the old cheat loop
	// test_signature_scan(); // Call the signature scan test
	test_hooking();        // Call the new hooking test
	return 0;
}

