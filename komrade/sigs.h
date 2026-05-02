#pragma once
// AOB signatures for AssaultCube 1.2.0.2 (ac_client.exe) verified patch sites.
// Each signature is a long-enough byte sequence around the call site to guarantee
// uniqueness. `Mask` uses 'x' for a literal byte and '?' for a wildcard (typically
// covering rebuild-volatile bytes such as relative call/jmp displacements).
//
// IMPORTANT: These signatures are seeded from the original 2-/10-/2-byte payloads
// the trainer already patches. They are LIKELY non-unique on their own and MUST be
// extended with surrounding context bytes by reading the live binary (use
// memory-cartographer to dump the 32 bytes around each site, then widen these
// signatures and re-validate AobScan returns exactly one match before relying on
// them). Until then, the trainer should fall back to the hard-coded module-relative
// offsets in offsets.h on AobScan failure.

namespace sigs
{
	// Ammo dec at module + 0x637E9 (FF 0E). Seed only; widen with surrounding bytes.
	inline constexpr unsigned char kAmmoDec[]    = { 0xFF, 0x0E };
	inline constexpr char          kAmmoDecMask[] = "xx";
	// Sub-offset from match start to the first byte to patch.
	inline constexpr unsigned      kAmmoDecPatchOffset = 0;

	// Recoil call sequence at module + 0x63786.
	inline constexpr unsigned char kRecoilCall[] = { 0x50, 0x8D, 0x4C, 0x24, 0x1C, 0x8B, 0xCE, 0xFF, 0xD2 };
	inline constexpr char          kRecoilCallMask[] = "xxxxxxxxx";
	inline constexpr unsigned      kRecoilCallPatchOffset = 0;

	// Grenade dec at module + 0x63378 (FF 08). Seed only; widen.
	inline constexpr unsigned char kGrenadeDec[] = { 0xFF, 0x08 };
	inline constexpr char          kGrenadeDecMask[] = "xx";
	inline constexpr unsigned      kGrenadeDecPatchOffset = 0;
}
