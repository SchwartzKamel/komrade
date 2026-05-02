// aimbot.h -- Tier 3 external aimbot for AssaultCube 1.2.0.2.
//
// Header-only. Include from komrade.cpp. Depends on offsets.h (for the
// LocalPlayer / yaw / pitch / position / HP offsets) and a live HANDLE to
// ac_client.exe with PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION.
//
// Strategy
// --------
// AC 1.2.0.2 has an EntityList at moduleBase+0x110118, but that vector holds
// flat map entities (pickups/spawns), NOT playerent pointers. The players
// vector address is unknown. So we sidestep it: VirtualQueryEx-walk RW commit
// pages in ac_client.exe and detect playerent-shaped structs by their
// distinctive backup-field pattern (HP at +0xBC mirrors HP at +0xD0, armor
// at +0xC0 mirrors armor at +0xD4) plus sane position floats.
//
// To avoid a full-heap scan every tick we cache the candidate list and
// re-scan at most once every kRescanIntervalMs. Each tick we re-validate
// cached pointers, pick the closest live enemy (i.e., not LocalPlayer, HP>0),
// compute target yaw/pitch from LocalPlayer position, and WPM them into
// LocalPlayer +0x40 / +0x44.
//
// Angle convention (verified empirically from the AC source: src/physics.cpp
// uses yaw 0 = +Y, increasing clockwise looking down; pitch 0 = horizon,
// positive = look up):
//     yaw   = atan2(dx, dy)         in degrees, normalized to [0, 360)
//     pitch = atan2(dz, dist2D)     in degrees, in [-90, 90]
// where (dx, dy, dz) = target.pos - source.pos.
// If pitch ends up inverted in-game, flip the sign on `dz` below.

#pragma once

#include <windows.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>

#include "offsets.h"

namespace aimbot {

constexpr uint32_t kRescanIntervalMs = 750;   // re-walk heap this often
constexpr uint32_t kMaxRegionBytes   = 16 * 1024 * 1024;  // skip huge mappings
constexpr uint32_t kPlayerStructMin  = 0x140; // need at least up to +0x10C
constexpr float    kPosBound         = 2000.0f;
constexpr int      kHpMax            = 200;

struct Snapshot
{
    uintptr_t addr;
    float     pos[3];
    int       hp;
};

inline bool ReadAt(HANDLE h, uintptr_t addr, void* buf, size_t n)
{
    SIZE_T got = 0;
    return ReadProcessMemory(h, (LPCVOID)addr, buf, n, &got) && got == n;
}

inline bool WriteAt(HANDLE h, uintptr_t addr, const void* buf, size_t n)
{
    SIZE_T put = 0;
    return WriteProcessMemory(h, (LPVOID)addr, buf, n, &put) && put == n;
}

// Returns true if the bytes at `addr` look like an AC playerent.
// Reads ~0x110 bytes once; cheap.
inline bool LooksLikePlayer(HANDLE h, uintptr_t addr, Snapshot& out)
{
    BYTE buf[0x110];
    if (!ReadAt(h, addr, buf, sizeof(buf))) return false;

    auto u32 = [&](size_t off) { return *reinterpret_cast<uint32_t*>(buf + off); };
    auto i32 = [&](size_t off) { return *reinterpret_cast<int32_t* >(buf + off); };
    auto f32 = [&](size_t off) { return *reinterpret_cast<float*   >(buf + off); };

    int hp     = i32(0xBC);
    int hpBak  = i32(0xD0);
    int armor  = i32(0xC0);
    int arBak  = i32(0xD4);

    // Backup pattern: HP and armor each mirror their post-spawn snapshot.
    if (hp != hpBak)         return false;
    if (armor != arBak)      return false;
    if (hp < 0 || hp > kHpMax) return false;
    if (armor < 0 || armor > kHpMax) return false;

    // Position must be sane and not all zero.
    float x = f32(0x04), y = f32(0x08), z = f32(0x0C);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    if (std::fabs(x) > kPosBound) return false;
    if (std::fabs(y) > kPosBound) return false;
    if (std::fabs(z) > kPosBound) return false;
    if (x == 0.0f && y == 0.0f && z == 0.0f) return false;

    // Yaw/pitch in expected ranges (cheap extra filter to kill false positives).
    float yaw = f32(0x40), pitch = f32(0x44);
    if (!std::isfinite(yaw) || yaw < -360.0f || yaw > 720.0f) return false;
    if (!std::isfinite(pitch) || pitch < -91.0f || pitch > 91.0f) return false;

    // Vtable-ish pointer at +0x00 should point somewhere sensible (non-zero,
    // not a tiny integer). Eliminates random heap chunks that happened to
    // line up.
    uint32_t vtbl = u32(0x00);
    if (vtbl < 0x00400000 || vtbl > 0x7FFFFFFF) return false;

    out.addr = addr;
    out.pos[0] = x; out.pos[1] = y; out.pos[2] = z;
    out.hp = hp;
    return true;
}

// Walk RW commit pages of `hProcess` and return all addresses where
// LooksLikePlayer succeeds. Excludes `excludeAddr` (the LocalPlayer struct).
inline std::vector<uintptr_t> ScanCandidates(HANDLE hProcess, uintptr_t excludeAddr)
{
    std::vector<uintptr_t> out;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t end = (uintptr_t)si.lpMaximumApplicationAddress;

    while (addr < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        const uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;

        bool wantRegion =
            mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0 &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            mbi.RegionSize >= kPlayerStructMin &&
            mbi.RegionSize <= kMaxRegionBytes &&
            mbi.Type != MEM_IMAGE;  // skip module RW (BSS/.data); players live on heap

        if (wantRegion)
        {
            // Read the whole region in one shot, then sweep at 4-byte stride.
            std::vector<BYTE> page(mbi.RegionSize);
            SIZE_T got = 0;
            if (ReadProcessMemory(hProcess, mbi.BaseAddress, page.data(),
                                  mbi.RegionSize, &got) && got >= kPlayerStructMin)
            {
                for (size_t off = 0; off + kPlayerStructMin <= got; off += 4)
                {
                    uintptr_t cand = (uintptr_t)mbi.BaseAddress + off;
                    if (cand == excludeAddr) continue;
                    Snapshot s{};
                    if (LooksLikePlayer(hProcess, cand, s))
                    {
                        out.push_back(cand);
                        // Step over the struct body so we don't double-match
                        // overlapping windows.
                        off += kPlayerStructMin - 4;
                    }
                }
            }
        }

        addr = regionEnd;
        if (regionEnd <= (uintptr_t)mbi.BaseAddress) break;  // overflow guard
    }

    return out;
}

// Compute angles (degrees) that aim from `src` at `tgt` using AC's convention.
inline void ComputeAngles(const float src[3], const float tgt[3],
                          float& outYaw, float& outPitch)
{
    constexpr float kRad2Deg = 57.29577951308232f;
    float dx = tgt[0] - src[0];
    float dy = tgt[1] - src[1];
    float dz = tgt[2] - src[2];

    float yaw = std::atan2(dx, dy) * kRad2Deg;
    if (yaw < 0.0f) yaw += 360.0f;

    float d2d = std::sqrt(dx * dx + dy * dy);
    float pitch = std::atan2(dz, d2d) * kRad2Deg;

    outYaw   = yaw;
    outPitch = pitch;
}

class Aimbot
{
public:
    Aimbot(HANDLE hProcess, uintptr_t moduleBase)
        : hProc_(hProcess), moduleBase_(moduleBase) {}

    // Returns true if angles were written this tick.
    bool Tick()
    {
        // Resolve LocalPlayer fresh each tick (struct can move between matches).
        uintptr_t lpPtrAddr = moduleBase_ + offs::verified::kLocalPlayerBase;
        uintptr_t lp = 0;
        if (!ReadAt(hProc_, lpPtrAddr, &lp, sizeof(lp)) || lp == 0) return false;

        Snapshot self{};
        if (!LooksLikePlayer(hProc_, lp, self)) return false;
        if (self.hp <= 0) return false;  // dead -- don't aim

        // Periodic re-scan.
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - lastScan_).count();
        if (elapsed > kRescanIntervalMs || candidates_.empty())
        {
            candidates_ = ScanCandidates(hProc_, lp);
            lastScan_   = now;
        }

        // Pick closest live candidate.
        const float* sp = self.pos;
        uintptr_t bestAddr = 0;
        float     bestD2   = 1e30f;
        Snapshot  bestSnap{};

        for (uintptr_t c : candidates_)
        {
            Snapshot s{};
            if (!LooksLikePlayer(hProc_, c, s)) continue;
            if (s.hp <= 0) continue;
            float dx = s.pos[0] - sp[0];
            float dy = s.pos[1] - sp[1];
            float dz = s.pos[2] - sp[2];
            float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 < bestD2)
            {
                bestD2  = d2;
                bestAddr = c;
                bestSnap = s;
            }
        }

        if (bestAddr == 0) return false;

        float yaw = 0.0f, pitch = 0.0f;
        ComputeAngles(self.pos, bestSnap.pos, yaw, pitch);

        const uintptr_t yawAddr   = lp + offs::verified::kViewYawChain[0];
        const uintptr_t pitchAddr = lp + offs::verified::kViewPitchChain[0];
        bool ok = WriteAt(hProc_, yawAddr,   &yaw,   sizeof(yaw));
        ok     &= WriteAt(hProc_, pitchAddr, &pitch, sizeof(pitch));
        return ok;
    }

    size_t LastCandidateCount() const { return candidates_.size(); }

private:
    HANDLE                                          hProc_;
    uintptr_t                                       moduleBase_;
    std::vector<uintptr_t>                          candidates_;
    std::chrono::steady_clock::time_point           lastScan_{};
};

} // namespace aimbot
