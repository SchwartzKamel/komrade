#include "stdafx.h"
#include "mem.h"
#include <vector>

bool mem::ReadMemoryEx(HANDLE hProcess, uintptr_t src, void* dst, size_t size)
{
	SIZE_T bytesRead = 0;
	return ReadProcessMemory(hProcess, (LPCVOID)src, dst, size, &bytesRead) && bytesRead == size;
}

bool mem::WriteMemoryEx(HANDLE hProcess, uintptr_t dst, const void* src, size_t size)
{
	SIZE_T bytesWritten = 0;
	return WriteProcessMemory(hProcess, (LPVOID)dst, src, size, &bytesWritten) && bytesWritten == size;
}

uintptr_t mem::DeepPointerEx(HANDLE hProcess, uintptr_t base, std::initializer_list<unsigned> offsets)
{
	uintptr_t addr = base;
	for (unsigned off : offsets)
	{
		if (!ReadProcessMemory(hProcess, (BYTE*)addr, &addr, sizeof(addr), nullptr))
		{
			return 0;
		}
		addr += off;
	}
	return addr;
}

void mem::PatchEx(BYTE* dst, BYTE* src, unsigned int size, HANDLE hProcess)
{
	DWORD oldprotect;
	VirtualProtectEx(hProcess, dst, size, PAGE_EXECUTE_READWRITE, &oldprotect);
	WriteProcessMemory(hProcess, dst, src, size, nullptr);
	VirtualProtectEx(hProcess, dst, size, oldprotect, &oldprotect);
}

void mem::NopEx(BYTE* dst, unsigned int size, HANDLE hProcess)
{
	BYTE* nopArray = new BYTE[size];
	memset(nopArray, 0x90, size);

	PatchEx(dst, nopArray, size, hProcess);
	delete[] nopArray;
}

size_t mem::GetModuleSize(DWORD procID, const wchar_t* modName)
{
	size_t moduleSize = 0;
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procID);
	if (hSnap != INVALID_HANDLE_VALUE)
	{
		MODULEENTRY32 modEntry;
		modEntry.dwSize = sizeof(modEntry);
		if (Module32First(hSnap, &modEntry))
		{
			do
			{
				if (!_wcsicmp(modEntry.szModule, modName))
				{
					moduleSize = modEntry.modBaseSize;
					break;
				}
			} while (Module32Next(hSnap, &modEntry));
		}
	}
	CloseHandle(hSnap);
	return moduleSize;
}

uintptr_t mem::AobScan(HANDLE hProcess,
                       uintptr_t moduleBase,
                       size_t moduleSize,
                       const BYTE* pattern,
                       const char* mask,
                       size_t patternLen)
{
	if (!hProcess || !moduleBase || !moduleSize || !pattern || !mask || !patternLen)
	{
		return 0;
	}

	const uintptr_t moduleEnd = moduleBase + moduleSize;
	uintptr_t currentAddress = moduleBase;

	while (currentAddress < moduleEnd)
	{
		MEMORY_BASIC_INFORMATION mbi = {};
		if (VirtualQueryEx(hProcess, (LPCVOID)currentAddress, &mbi, sizeof(mbi)) == 0)
		{
			break;
		}

		const uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
		const size_t regionSize = mbi.RegionSize;

		// Skip regions outside our module range
		if (regionBase >= moduleEnd)
		{
			break;
		}

		// Only scan committed, readable regions
		const bool isCommitted = (mbi.State == MEM_COMMIT);
		const bool isReadable = (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
		                                        PAGE_READONLY | PAGE_READWRITE |
		                                        PAGE_EXECUTE_WRITECOPY | PAGE_WRITECOPY |
		                                        PAGE_EXECUTE)) != 0;
		const bool isGuarded = (mbi.Protect & PAGE_GUARD) != 0;

		if (isCommitted && isReadable && !isGuarded)
		{
			// Clamp region to module bounds
			const uintptr_t scanStart = (regionBase < moduleBase) ? moduleBase : regionBase;
			const uintptr_t scanEnd = ((regionBase + regionSize) > moduleEnd) ? moduleEnd : (regionBase + regionSize);
			const size_t scanSize = (scanEnd > scanStart) ? (scanEnd - scanStart) : 0;

			if (scanSize >= patternLen)
			{
				std::vector<BYTE> buffer(scanSize);
				SIZE_T bytesRead = 0;
				if (ReadProcessMemory(hProcess, (LPCVOID)scanStart, buffer.data(), scanSize, &bytesRead))
				{
					// Linear scan for pattern with mask
					for (size_t i = 0; i <= bytesRead - patternLen; ++i)
					{
						bool found = true;
						for (size_t j = 0; j < patternLen; ++j)
						{
							if (mask[j] == 'x' && buffer[i + j] != pattern[j])
							{
								found = false;
								break;
							}
						}
						if (found)
						{
							return scanStart + i;
						}
					}
				}
			}
		}

		// Move to next region
		currentAddress = regionBase + regionSize;
	}

	return 0;
}