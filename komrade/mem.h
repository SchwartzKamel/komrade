#pragma once

// API surface intentionally mirrors libmem (https://github.com/rdbo/libmem) so a
// future swap is mechanical. libmem itself is AGPLv3 and is NOT linked here;
// this trainer is MIT.

#include <initializer_list>

namespace mem
{

	void PatchEx(BYTE* dst, BYTE* src, unsigned int size, HANDLE hProcess);
	void NopEx(BYTE* dst, unsigned int size, HANDLE hProcess);

	// External AOB scan. Searches the readable, committed pages of [moduleBase, moduleBase+moduleSize)
	// in the target process for `pattern` (length `patternLen`). `mask` is a C-string of '?' (wildcard)
	// or 'x' (must match) of length `patternLen`. Returns the absolute address of the first match,
	// or 0 if none found / on RPM error. Uses VirtualQueryEx to skip non-readable regions.
	// Mirrors libmem's LM_SigScanEx.
	uintptr_t AobScan(HANDLE hProcess,
	                  uintptr_t moduleBase,
	                  size_t moduleSize,
	                  const BYTE* pattern,
	                  const char* mask,
	                  size_t patternLen);

	// Convenience: returns the size of `modName` loaded in `procID` via Toolhelp32.
	// Returns 0 on failure.
	size_t GetModuleSize(DWORD procID, const wchar_t* modName);

	// --- libmem-mirrored thin wrappers -----------------------------------
	// LM_ReadMemoryEx → ReadMemoryEx. Returns true iff the full `size` was read.
	bool ReadMemoryEx(HANDLE hProcess, uintptr_t src, void* dst, size_t size);

	// LM_WriteMemoryEx → WriteMemoryEx. Returns true iff the full `size` was
	// written. Wraps WriteProcessMemory; does not toggle protection (use
	// PatchEx for code-patching where protection must change).
	bool WriteMemoryEx(HANDLE hProcess, uintptr_t dst, const void* src, size_t size);

	// LM_DeepPointerEx → DeepPointerEx. Walks a pointer chain rooted at `base`
	// in the target process, applying each offset after dereferencing.
	// Equivalent to FindDMAAddy with an initializer_list ergonomic shape.
	uintptr_t DeepPointerEx(HANDLE hProcess, uintptr_t base, std::initializer_list<unsigned> offsets);
}