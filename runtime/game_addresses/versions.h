// The supported game versions, by the main program's build ID (exefs/main, 0x40..0x53). Each one has its own
// address tables: game_addresses/patches_<key>.h and runtime_<key>.h, with the same names. An unknown build ID is
// refused: there is no fallback table. tools/game_addresses.py reads this same file.
// An X-macro list (no include guard): GA_VERSION(key, label, main build ID) and
// GA_RELEASE(key, label, SHA-256 of exefs/main.npdm).
//
// 1.0.1 and 1.1.0 share one main program (and the same romfs), so they share one table and one build of the game
// code. Only their main.npdm differs: GA_RELEASE tells them apart for the start screen and the log.

// SkywardRecomp (rebuild): Skyward Sword HD. The v1_0_0 entry's build ID is SS HD 1.0.0's. The address tables
// (patches_v1_0_0.h / runtime_v1_0_0.h) hold only Link's Awakening leftover names, all 0 by design, until SS
// features add verified names.

GA_VERSION(v1_0_0, "1.0.0",         "D824D4B1BBD45D03668380D6D041BA4CA00A64FF")
