// komrade.cpp : Kernel-Outside Memory Reader And Debug Engine
//   External trainer for AssaultCube 1.2.0.2 (ac_client.exe, x86).
// Scope: offline / single-player / LAN / private-server use only.
//
// Verified call sites (AC 1.2.0.2): see offsets.h (offs::verified::*).
//   LocalPlayer  = moduleBase + kLocalPlayerBase   (pointer to local player struct)
//   health       = *LocalPlayer + kHealthChain
//   armor        = *LocalPlayer + kArmorChain
//   ammo dec/inc = moduleBase + kAmmoDecSite       (FF 0E -> FF 06 to invert)
//   recoil call  = moduleBase + kRecoilCallSite    (10-byte sequence -> NOPs)
//   grenade dec  = moduleBase + kGrenadeDecSite    (FF 08 -> 90 90)

#include "stdafx.h"
#include <atomic>
#include <thread>
#include <chrono>
#include "proc.h"
#include "mem.h"
#include "offsets.h"
#include "sigs.h"
#include "aimbot.h"

namespace {

	// Original bytes for code patches, used to restore on toggle-off / clean exit.
	constexpr BYTE kAmmoOriginal[2]    = { 0xFF, 0x0E };               // dec [esi]
	constexpr BYTE kAmmoPatched[2]     = { 0xFF, 0x06 };               // inc [esi]
	constexpr BYTE kRecoilOriginal[10] = { 0x50, 0x8D, 0x4C, 0x24, 0x1C, 0x8B, 0xCE, 0xFF, 0xD2, 0x90 };
	constexpr BYTE kRecoilNop[10]      = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	constexpr BYTE kGrenadeOriginal[2] = { 0xFF, 0x08 };               // dec [eax]
	constexpr BYTE kGrenadeNop[2]      = { 0x90, 0x90 };

	constexpr int  kFreezeValue = 1337;

	// Minimum access mask the trainer actually needs.
	constexpr DWORD kAccessMask =
		PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION;

	bool KeyPressed(int vk)
	{
		// Edge-trigger: returns true once per physical key press.
		return (GetAsyncKeyState(vk) & 1) != 0;
	}

	enum class FeatureKind { CodePatch, FreezeWrite };

	// One row per feature. Hotkey, kind, and payload colocated.
	struct Feature
	{
		const char*       name;
		int               hotkey;       // VK_*
		FeatureKind       kind;

		// CodePatch payload (kind == CodePatch).
		uintptr_t         siteOffset;   // module-relative seed; resolved to siteAddr at startup.
		uintptr_t         siteAddr;     // absolute, resolved (AobScan or moduleBase + siteOffset).
		const BYTE*       originalBytes;
		const BYTE*       patchedBytes; // toggle-on bytes
		size_t            patchLen;

		// AOB seed for fallback-aware resolution (CodePatch only).
		const BYTE*       sigPattern;
		const char*       sigMask;
		size_t            sigLen;

		// FreezeWrite payload (kind == FreezeWrite).
		uintptr_t         freezeAddr;   // resolved at startup; 0 = unresolved/skip
		const void*       freezeValue;
		size_t            freezeSize;

		std::atomic<bool> enabled;
	};

	// Resolve a code-patch site: try AobScan first, fall back to moduleBase+offset.
	// Logs which path was used and warns if AobScan diverges from the hard-coded site.
	uintptr_t ResolveCodeSite(Feature& f,
	                          HANDLE hProcess,
	                          uintptr_t moduleBase,
	                          size_t moduleSize)
	{
		const uintptr_t fallback = moduleBase + f.siteOffset;
		uintptr_t scanned = 0;
		if (f.sigPattern && f.sigMask && f.sigLen)
		{
			scanned = mem::AobScan(hProcess, moduleBase, moduleSize,
			                       f.sigPattern, f.sigMask, f.sigLen);
		}

		if (scanned == 0)
		{
			std::cout << "[" << f.name << "] AobScan: no match (seed signature is non-unique by design); "
			          << "using hard-coded offset 0x" << std::hex << f.siteOffset << std::dec << ".\n";
			return fallback;
		}

		if (scanned != fallback)
		{
			std::cerr << "[" << f.name << "] WARNING: AobScan match 0x" << std::hex << scanned
			          << " diverges from hard-coded site 0x" << fallback
			          << " — possible AC build drift. Using hard-coded site.\n" << std::dec;
			return fallback;
		}

		std::cout << "[" << f.name << "] AobScan: match at 0x" << std::hex << scanned
		          << " (matches hard-coded site).\n" << std::dec;
		return scanned;
	}

}

// Demo: read & write the ammo value via a resolved pointer chain.
static int leet_ammo()
{
	DWORD procID = GetProcID(L"ac_client.exe");
	if (!procID)
	{
		std::cout << "ac_client.exe not running.\n";
		return 1;
	}

	uintptr_t moduleBase = GetModuleBaseAddress(procID, L"ac_client.exe");
	if (!moduleBase)
	{
		std::cout << "Failed to resolve module base.\n";
		return 1;
	}

	HANDLE hProcess = OpenProcess(kAccessMask, FALSE, procID);
	if (!hProcess)
	{
		std::cout << "OpenProcess failed: " << GetLastError() << "\n";
		return 1;
	}

	uintptr_t dynamicPtrBaseAddr = moduleBase + 0x10F4F4;
	std::cout << "DynamicPtrBaseAddr = 0x" << std::hex << dynamicPtrBaseAddr << std::endl;

	std::vector<unsigned int> ammoOffsets = { 0x374, 0x14, 0x0 };
	uintptr_t ammoAddr = FindDMAAddy(hProcess, dynamicPtrBaseAddr, ammoOffsets);
	std::cout << "ammoAddr = 0x" << std::hex << ammoAddr << std::endl;

	int ammoValue = 0;
	if (ReadProcessMemory(hProcess, (BYTE*)ammoAddr, &ammoValue, sizeof(ammoValue), nullptr))
	{
		std::cout << "Current ammo = " << std::dec << ammoValue << std::endl;
	}

	int newAmmo = 1337;
	WriteProcessMemory(hProcess, (BYTE*)ammoAddr, &newAmmo, sizeof(newAmmo), nullptr);

	if (ReadProcessMemory(hProcess, (BYTE*)ammoAddr, &ammoValue, sizeof(ammoValue), nullptr))
	{
		std::cout << "New ammo = " << std::dec << ammoValue << std::endl;
	}

	CloseHandle(hProcess);
	return 0;
}

// Main trainer loop: hotkey-driven freezes and code patches.
static int RunTrainer()
{
	DWORD procID = GetProcID(L"ac_client.exe");
	if (!procID)
	{
		std::cerr << "ac_client.exe not running. Press enter to exit.\n";
		getchar();
		return 1;
	}

	HANDLE hProcess = OpenProcess(kAccessMask, FALSE, procID);
	if (!hProcess)
	{
		std::cerr << "OpenProcess failed: " << GetLastError() << ". Press enter to exit.\n";
		getchar();
		return 1;
	}

	uintptr_t moduleBase = GetModuleBaseAddress(procID, L"ac_client.exe");
	if (!moduleBase)
	{
		std::cerr << "Failed to resolve module base. Press enter to exit.\n";
		CloseHandle(hProcess);
		getchar();
		return 1;
	}

	const size_t moduleSize = mem::GetModuleSize(procID, L"ac_client.exe");
	if (!moduleSize)
	{
		std::cerr << "Failed to resolve module size; AobScan disabled.\n";
	}

	// Resolve freeze targets via the pointer chain.
	const uintptr_t localPlayerPtr = moduleBase + offs::verified::kLocalPlayerBase;
	const uintptr_t healthAddr = mem::DeepPointerEx(hProcess, localPlayerPtr, { offs::verified::kHealthChain[0] });
	const uintptr_t armorAddr  = mem::DeepPointerEx(hProcess, localPlayerPtr, { offs::verified::kArmorChain[0]  });

	// ----- Feature table ------------------------------------------------
	// Order: health (NUMPAD0), armor (NUMPAD1), ammo (F2), recoil (F3), grenade (F4).
	enum FeatureId { F_HEALTH = 0, F_ARMOR, F_AMMO, F_RECOIL, F_GRENADE, F_COUNT };

	Feature features[F_COUNT];

	// Freeze: health
	features[F_HEALTH].name        = "Health Freeze";
	features[F_HEALTH].hotkey      = VK_NUMPAD0;
	features[F_HEALTH].kind        = FeatureKind::FreezeWrite;
	features[F_HEALTH].freezeAddr  = healthAddr;
	features[F_HEALTH].freezeValue = &kFreezeValue;
	features[F_HEALTH].freezeSize  = sizeof(kFreezeValue);
	features[F_HEALTH].enabled.store(false);

	// Freeze: armor
	features[F_ARMOR].name        = "Armor Freeze";
	features[F_ARMOR].hotkey      = VK_NUMPAD1;
	features[F_ARMOR].kind        = FeatureKind::FreezeWrite;
	features[F_ARMOR].freezeAddr  = armorAddr;
	features[F_ARMOR].freezeValue = &kFreezeValue;
	features[F_ARMOR].freezeSize  = sizeof(kFreezeValue);
	features[F_ARMOR].enabled.store(false);

	// CodePatch: ammo no-decrement (FF 0E -> FF 06)
	features[F_AMMO].name          = "Ammo Patch";
	features[F_AMMO].hotkey        = VK_F2;
	features[F_AMMO].kind          = FeatureKind::CodePatch;
	features[F_AMMO].siteOffset    = offs::verified::kAmmoDecSite;
	features[F_AMMO].originalBytes = kAmmoOriginal;
	features[F_AMMO].patchedBytes  = kAmmoPatched;
	features[F_AMMO].patchLen      = sizeof(kAmmoOriginal);
	features[F_AMMO].sigPattern    = sigs::kAmmoDec;
	features[F_AMMO].sigMask       = sigs::kAmmoDecMask;
	features[F_AMMO].sigLen        = sizeof(sigs::kAmmoDec);
	features[F_AMMO].enabled.store(false);

	// CodePatch: no recoil (10-byte NOP slide)
	features[F_RECOIL].name          = "Recoil Patch";
	features[F_RECOIL].hotkey        = VK_F3;
	features[F_RECOIL].kind          = FeatureKind::CodePatch;
	features[F_RECOIL].siteOffset    = offs::verified::kRecoilCallSite;
	features[F_RECOIL].originalBytes = kRecoilOriginal;
	features[F_RECOIL].patchedBytes  = kRecoilNop;
	features[F_RECOIL].patchLen      = sizeof(kRecoilOriginal);
	features[F_RECOIL].sigPattern    = sigs::kRecoilCall;
	features[F_RECOIL].sigMask       = sigs::kRecoilCallMask;
	features[F_RECOIL].sigLen        = sizeof(sigs::kRecoilCall);
	features[F_RECOIL].enabled.store(false);

	// CodePatch: grenade no-decrement (FF 08 -> 90 90)
	features[F_GRENADE].name          = "Grenade Patch";
	features[F_GRENADE].hotkey        = VK_F4;
	features[F_GRENADE].kind          = FeatureKind::CodePatch;
	features[F_GRENADE].siteOffset    = offs::verified::kGrenadeDecSite;
	features[F_GRENADE].originalBytes = kGrenadeOriginal;
	features[F_GRENADE].patchedBytes  = kGrenadeNop;
	features[F_GRENADE].patchLen      = sizeof(kGrenadeOriginal);
	features[F_GRENADE].sigPattern    = sigs::kGrenadeDec;
	features[F_GRENADE].sigMask       = sigs::kGrenadeDecMask;
	features[F_GRENADE].sigLen        = sizeof(sigs::kGrenadeDec);
	features[F_GRENADE].enabled.store(false);

	// ----- Resolve / validate per-feature addresses ---------------------
	for (int i = 0; i < F_COUNT; ++i)
	{
		Feature& f = features[i];
		if (f.kind == FeatureKind::CodePatch)
		{
			if (moduleSize)
			{
				f.siteAddr = ResolveCodeSite(f, hProcess, moduleBase, moduleSize);
			}
			else
			{
				f.siteAddr = moduleBase + f.siteOffset;
				std::cout << "[" << f.name << "] AobScan skipped (no module size); "
				          << "using hard-coded offset.\n";
			}
		}
		else
		{
			if (f.freezeAddr == 0)
			{
				std::cerr << "[" << f.name << "] freeze address unresolved; feature disabled.\n";
			}
		}
	}

	std::cout << "\n"
	          << "  __ _____  __  __ ___   ___    ___  ____\n"
	          << " / //_/ _ \\/  |/  / _ \\ / _ | / _ \\/ __/\n"
	          << "/ ,< / // / /|_/ / , _// __ |/ // / _/  \n"
	          << "_/|_|\\___/_/  /_/_/|_|/_/ |_/____/___/  \n"
	          << "                       AC 1.2.0.2 trainer\n\n"
	          << "Komrade ready.\n"
	          << "  NUMPAD0 : freeze health\n"
	          << "  NUMPAD1 : freeze armor\n"
	          << "  NUMPAD2 : aimbot (closest live enemy by heap-scan)\n"
	          << "  F2      : ammo no-decrement\n"
	          << "  F3      : no recoil\n"
	          << "  F4      : grenade no-decrement\n"
	          << "  NUMPAD9 : exit (restores all patches)\n";

	// ----- Aimbot worker thread (Tier 3) --------------------------------
	std::atomic<bool> aimbotOn(false);
	aimbot::Aimbot aimbotEngine(hProcess, moduleBase);
	std::atomic<bool> g_running(true);
	std::thread aimbotWorker([&]() {
		int loggedCount = -1;
		while (g_running.load(std::memory_order_acquire))
		{
			if (aimbotOn.load(std::memory_order_acquire))
			{
				aimbotEngine.Tick();
				int n = (int)aimbotEngine.LastCandidateCount();
				if (n != loggedCount)
				{
					std::cout << "[Aimbot] tracking " << n << " candidate(s).\n";
					loggedCount = n;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	});

	// ----- Freeze worker thread -----------------------------------------
	std::thread freezeWorker([&]() {
		while (g_running.load(std::memory_order_acquire))
		{
			for (int i = 0; i < F_COUNT; ++i)
			{
				Feature& f = features[i];
				if (f.kind != FeatureKind::FreezeWrite) continue;
				if (!f.enabled.load(std::memory_order_acquire)) continue;
				if (f.freezeAddr == 0) continue;
				mem::PatchEx((BYTE*)f.freezeAddr, (BYTE*)f.freezeValue,
				             (unsigned)f.freezeSize, hProcess);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	});

	// ----- Main input loop ----------------------------------------------
	DWORD dwExit = 0;
	while (GetExitCodeProcess(hProcess, &dwExit) && dwExit == STILL_ACTIVE)
	{
		if (KeyPressed(VK_NUMPAD9)) break;

		if (KeyPressed(VK_NUMPAD2))
		{
			const bool now = !aimbotOn.load();
			aimbotOn.store(now, std::memory_order_release);
			std::cout << "Aimbot: " << (now ? "ON" : "OFF") << std::endl;
		}

		for (int i = 0; i < F_COUNT; ++i)
		{
			Feature& f = features[i];
			if (!KeyPressed(f.hotkey)) continue;

			if (f.kind == FeatureKind::FreezeWrite)
			{
				if (f.freezeAddr == 0)
				{
					std::cerr << "[" << f.name << "] cannot toggle: address unresolved.\n";
					continue;
				}
				const bool now = !f.enabled.load();
				f.enabled.store(now, std::memory_order_release);
				std::cout << f.name << ": " << (now ? "ON" : "OFF") << std::endl;
			}
			else // CodePatch
			{
				const bool now = !f.enabled.load();
				const BYTE* bytes = now ? f.patchedBytes : f.originalBytes;
				mem::PatchEx((BYTE*)f.siteAddr, (BYTE*)bytes,
				             (unsigned)f.patchLen, hProcess);
				f.enabled.store(now, std::memory_order_release);
				std::cout << f.name << ": " << (now ? "ON" : "OFF") << std::endl;
			}
		}

		Sleep(10);
	}

	// ----- Clean exit ---------------------------------------------------
	g_running.store(false, std::memory_order_release);
	if (freezeWorker.joinable()) freezeWorker.join();
	if (aimbotWorker.joinable()) aimbotWorker.join();

	// Restore any active code patches so the game stays sane.
	for (int i = 0; i < F_COUNT; ++i)
	{
		Feature& f = features[i];
		if (f.kind != FeatureKind::CodePatch) continue;
		if (!f.enabled.load()) continue;
		mem::PatchEx((BYTE*)f.siteAddr, (BYTE*)f.originalBytes,
		             (unsigned)f.patchLen, hProcess);
	}

	CloseHandle(hProcess);
	std::cout << "Komrade exited cleanly.\n";
	return 0;
}

int main()
{
	leet_ammo();
	RunTrainer();
	return 0;
}
