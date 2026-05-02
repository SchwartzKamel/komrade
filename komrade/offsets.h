#pragma once
// Offsets and pointer chains for AssaultCube 1.2.0.2 (ac_client.exe, x86).
//
// Three sections:
//   1. Verified — exercised by the current trainer and produces the expected
//      in-game effect.
//   2. A200K — sourced from https://github.com/A200K/AssaultCube_Hack
//      (Offsets.h). Authoritative for AC 1.2.0.2 but NOT yet exercised by
//      this trainer. A200K's values are file-relative (PE image base
//      0x00400000); we store the module-relative form (subtracting 0x400000).
//      Cross-validated: A200K PATCHRECOIL 0x463786 → 0x63786, which matches
//      this repo's existing kRecoilSiteOffset exactly. Trust the rebase.
//   3. Candidate — community/tutorial offsets, lower confidence. Verify
//      before promoting.
//
// Convention:
//   - All offsets are `constexpr uintptr_t` (module-relative) or
//     `constexpr unsigned[]` (pointer-chain offsets).
//   - Module-relative: `moduleBase + kThing` at the call site.
//   - Pointer chains: pass to `FindDMAAddy` (or future `mem::DeepPointerEx`).
//
// Scope: offline / single-player / LAN / private-server only.

#include <cstdint>

namespace offs
{
    // ============================================================
    // 1. Verified — landed and exercised by the current trainer.
    // ============================================================
    namespace verified
    {
        // LocalPlayer static pointer.
        // VERIFIED LIVE: 0x17E0A8 (the previous legacy value) is OUT OF RANGE
        // for ac_client.exe whose image is only 0x13F000 bytes — it cannot be
        // a static pointer. 0x109B74 (a200k::kDirectCurrentPlayer) was probed
        // against the live process and dereferences to a real playerent (see
        // tools/windbg/dump_player_struct.ps1).
        inline constexpr uintptr_t kLocalPlayerBase   = 0x00109B74;

        // Pointer-chain offsets from *LocalPlayer.
        // VERIFIED LIVE via dump_player_struct.ps1: health and armor sit at
        // +0xBC / +0xC0, with last-saved backups at +0xD0 / +0xD4. The old
        // +0xEC / +0xF0 values (common in older AC writeups) point at
        // pointer fields in this build, not ints.
        inline constexpr unsigned  kHealthChain[]     = { 0xBC };
        inline constexpr unsigned  kArmorChain[]      = { 0xC0 };

        // VERIFIED LIVE 2026-05 (validate_all.ps1 13/13 PASS):
        //   *LocalPlayer +0x04..+0x0C = world position (X,Y,Z floats)
        //   *LocalPlayer +0x40        = yaw   in [0,360)
        //   *LocalPlayer +0x44        = pitch in [-90,90]
        //   *LocalPlayer +0x48        = roll  in [-90,90]
        // The older 0x34/0x38 yaw/pitch cite is wrong for AC 1.2.0.2.
        inline constexpr unsigned  kPlayerPosXChain[] = { 0x04 };
        inline constexpr unsigned  kPlayerPosYChain[] = { 0x08 };
        inline constexpr unsigned  kPlayerPosZChain[] = { 0x0C };
        inline constexpr unsigned  kViewYawChain[]    = { 0x40 };
        inline constexpr unsigned  kViewPitchChain[]  = { 0x44 };
        inline constexpr unsigned  kViewRollChain[]   = { 0x48 };

        // Code patch sites (module-relative).
        inline constexpr uintptr_t kAmmoDecSite       = 0x637E9;   // FF 0E -> FF 06 inverts
        inline constexpr uintptr_t kRecoilCallSite    = 0x63786;   // 10-byte NOP slide  (== a200k::kPatchRecoil)
        inline constexpr uintptr_t kGrenadeDecSite    = 0x63378;   // FF 08 -> 90 90

        // Demo: ammo via dynamic pointer chain (used by leet_ammo()).
        inline constexpr uintptr_t kAmmoDemoStaticPtr = 0x10F4F4;
        inline constexpr unsigned  kAmmoDemoChain[]   = { 0x374, 0x14, 0x0 };
    }

    // ============================================================
    // 2. A200K — authoritative for AC 1.2.0.2.
    //    Source: https://github.com/A200K/AssaultCube_Hack
    //    All values converted from file-relative (PE base 0x400000)
    //    to module-relative by subtracting 0x400000.
    //    Rebase validated against verified::kRecoilCallSite.
    // ============================================================
    namespace a200k
    {
        // -- Functions (call/hook targets) --
        inline constexpr uintptr_t kGetWeaponStr      = 0x061820;  // 0x461820
        inline constexpr uintptr_t kCallVote          = 0x028640;  // 0x428640
        inline constexpr uintptr_t kSCallVote         = 0x028860;  // 0x428860
        inline constexpr uintptr_t kSetVar            = 0x064FE0;  // 0x464FE0
        inline constexpr uintptr_t kIsVisible         = 0x08ABD0;  // 0x48ABD0
        inline constexpr uintptr_t kTraceLine         = 0x08A310;  // 0x48A310
        inline constexpr uintptr_t kGl_DrawHud        = 0x00AAF0;  // 0x40AAF0
        inline constexpr uintptr_t kHudOutf           = 0x0090F0;  // 0x4090F0
        inline constexpr uintptr_t kDrawLine          = 0x0045F0;  // 0x4045F0
        inline constexpr uintptr_t kDrawBlendBox      = 0x004B30;  // 0x404B30
        inline constexpr uintptr_t kDynFov            = 0x005330;  // 0x405330
        inline constexpr uintptr_t kToServer          = 0x020210;  // 0x420210
        inline constexpr uintptr_t kSendMsg           = 0x0204B0;  // 0x4204B0
        inline constexpr uintptr_t kGiveDamage        = 0x0269F0;  // 0x4269F0
        inline constexpr uintptr_t kSendKill          = 0x026C60;  // 0x426C60
        inline constexpr uintptr_t kRenderSpotIcon   = 0x005020;  // 0x405020
        inline constexpr uintptr_t kEngineDrawText    = 0x01A150;  // 0x41A150
        inline constexpr uintptr_t kGunAttack         = 0x063600;  // 0x463600 — kAmmoDecSite (0x637E9) lives inside this
        inline constexpr uintptr_t kGl_DrawHudMidFunc = 0x00C375;  // 0x40C375

        // -- Variables (static module-relative addresses) --
        inline constexpr uintptr_t kGameMode          = 0x10F49C;  // 0x50F49C
        inline constexpr uintptr_t kLastMillis        = 0x109EAC;  // 0x509EAC
        inline constexpr uintptr_t kEntityList        = 0x110118;  // 0x510118 — pointer to dynamic player array
        inline constexpr uintptr_t kToggleEdit        = 0x04C9B0;  // 0x44C9B0
        inline constexpr uintptr_t kEditMode          = 0x10A1AB;  // 0x50A1AB
        inline constexpr uintptr_t kFovY              = 0x101BAC;  // 0x501BAC — float, default ~90.0
        inline constexpr uintptr_t kAspect            = 0x101BB0;  // 0x501BB0
        inline constexpr uintptr_t kScreenSettings    = 0x110C94;  // 0x510C94
        inline constexpr uintptr_t kGame              = 0x10F45C;  // 0x50F45C
        inline constexpr uintptr_t kMvpMatrix         = 0x101AE8;  // 0x501AE8 — float[16], view-projection
        inline constexpr uintptr_t kMvMatrix          = 0x101B28;  // 0x501B28
        inline constexpr uintptr_t kProjMatrix        = 0x101BB8;  // 0x501BB8
        inline constexpr uintptr_t kCurrentMapName    = 0x109EC0;  // 0x509EC0 — char[16]
        inline constexpr uintptr_t kCurrentTextField  = 0x0FEC18;  // 0x4FEC18
        // Authoritative LocalPlayer pointer per A200K. If verified::kLocalPlayerBase
        // doesn't actually drive health/armor in-game, this is the value to try.
        inline constexpr uintptr_t kDirectCurrentPlayer = 0x109B74; // 0x509B74
        inline constexpr uintptr_t kPatchRecoil       = 0x063786;  // 0x463786 — equals verified::kRecoilCallSite
        inline constexpr uintptr_t kSFactor           = 0x105BB4;  // 0x505BB4
        inline constexpr uintptr_t kWorld             = 0x10A1F8;  // 0x50A1F8
        inline constexpr uintptr_t kBounceEnts        = 0x110A28;  // 0x510A28
    }

    // ============================================================
    // 3. Candidate — community/tutorial offsets, lower confidence.
    //    Verify (mutate-and-reread) before promoting to verified.
    // ============================================================
    namespace candidate
    {
        // UNVERIFIED: per-weapon current ammo pointer chain from LocalPlayer.
        // Mirrors the demo chain but typed as a feature offset.
        // TODO: switch weapons in-game and confirm the value reflects the
        // active weapon's clip ammo.
        inline constexpr unsigned  kCurrentWeaponAmmoChain[] = { 0x374, 0x14, 0x0 };

        // VERIFIED LIVE 2025-XX (cdb dump, ac_iceroad menu BG):
        //   +0x40 = 272.0  (yaw, in [0,360])
        //   +0x44 = 0.0    (pitch, in [-90,90])
        //   +0x48 = 0.0    (roll)
        // The older 0x34/0x38 cite is wrong for AC 1.2.0.2 — those offsets
        // sit inside the head-position vec3 (+0x34..+0x3C).
        inline constexpr unsigned  kViewYawChain[]   = { 0x40 };
        inline constexpr unsigned  kViewPitchChain[] = { 0x44 };
        inline constexpr unsigned  kViewRollChain[]  = { 0x48 };

        // VERIFIED LIVE 2025-XX (cdb dump): position vec3 at +0x04..+0x0C.
        //   +0x04 = X (e.g. 360.5)
        //   +0x08 = Y (e.g. 371.0)
        //   +0x0C = Z (e.g. 8.5)
        inline constexpr unsigned  kPlayerPosXChain[] = { 0x4 };
        inline constexpr unsigned  kPlayerPosYChain[] = { 0x8 };
        inline constexpr unsigned  kPlayerPosZChain[] = { 0xC };

        // UNVERIFIED: player name (null-terminated char[]) on LocalPlayer.
        // TODO: read string at this offset and confirm it matches the
        // in-game name.
        inline constexpr unsigned  kPlayerNameChain[] = { 0x225 };

        // UNVERIFIED: auto-shoot / rapid-fire code patch site near gun_attack.
        // a200k::kGunAttack is at 0x063600; a NOP candidate sits ~0x140 after it.
        // TODO: disassemble around kGunAttack to find the conditional branch
        // gating semi-auto fire, then test the patch.
        inline constexpr uintptr_t kAutoShootSite = 0x63740;
    }
}
