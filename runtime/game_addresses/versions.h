// The supported game versions, by the main program's build ID (exefs/main, 0x40..0x53). Each one has its own
// address tables: game_addresses/patches_<key>.h and runtime_<key>.h, with the same names. An unknown build ID is
// refused: there is no fallback table. tools/game_addresses.py reads this same file.
// An X-macro list (no include guard): GA_VERSION(key, label, main build ID) and
// GA_RELEASE(key, label, SHA-256 of exefs/main.npdm).
//
// SkywardRecomp (rebuild): Skyward Sword HD. v1_0_0 is the base game, v1_0_1 the update over it (title
// 01002DA013484800, v65536): its main program is another build, so it has its own table and its own generated game
// code. Their address tables (patches_v1_0_0.h / runtime_v1_0_0.h and the v1_0_1 ones) hold only Link's Awakening
// leftover names, all 0 by design, until SS features add verified names.

GA_VERSION(v1_0_0, "1.0.0",         "D824D4B1BBD45D03668380D6D041BA4CA00A64FF")
GA_VERSION(v1_0_1, "1.0.1",         "5441AF3C89F97E1D25A8CCF14C742BF82336D8A8")
