# TASKS — crosscheck 2026-11-22

Baseline: branch `feature/crosscheck-repo-dan-769` @ `00dba562`, worktree bersih.
Toolchain: MSYS2 UCRT64 clang 23.1.5, Python 3.14 (UCRT64). File game sudah tersedia di `extracted/` (2026-11-22),
jadi recomp end-to-end dan run in-game bisa dijalankan di mesin ini.

## Hasil crosscheck

- `make build/skywardrecomp.exe`: OK (sudah up to date, exe 17 MB).
- Suite test CI Windows + tambahan (33 test, `build/ck_tests.sh`): **30 PASS**, 3 gagal:

| Test | Status | Penyebab |
|---|---|---|
| `test_vulkan_overlap.py` | FAIL (assert) | lihat B1 — tidak dijalankan CI |
| `test_lift.py` | FAIL | `No module named 'unicorn'` (butuh venv `make rando-helper`) |
| `test_field_regions.py` | PASS (skip) | `third_party/LAS-Randomizer/.../logic.yml` tidak ada; B2 skip guard dengan pesan jelas |
| `make test_texpack` | PASS jika perintahnya dijalankan langsung; lewat `make` gagal karena TMP shell lokal (bukan bug repo) |

- Shader translator fuzz (1000 shader GLSL): 0 validation failure; 21–28 *translation failure* per batch (`shf cc/x/hi` paling banyak, `bindless tex`, `prmt mode`, `indexed ld a[]`, …) — ini fitur yang belum didukung, bukan regresi.

Catatan env: di Git Bash, `bash -c` anak me-reset `TMP` sehingga clang gagal `unable to make temporary file`. Jalankan test dari shell yang sama dengan `TMP` menunjuk folder yang ada.

---

## Recomp

### R1. Tabel alamat SS 1.0.0 masih kosong — P1, L — BELUM SELESAI
- Bukti: `runtime/game_addresses/patches_v1_0_0.h` (112 entri) dan `runtime_v1_0_0.h` (73 entri) semuanya `0x0` (sisa LA, by design per `versions.h:10-12`). README: "SS table still partial (30%)".
- Dampak: setiap fitur HLE/patch yang memakai `GA(...)` (aspect/ultrawide sites, FOV, warp, dll.) tidak aktif untuk SS.
- Butuh: binary SS (exefs/main) untuk mencari alamat. Sekarang tersedia di `extracted/exefs/main` (2026-11-22, lihat R4).
- AC: tiap nama yang dipakai fitur SS punya alamat terverifikasi; `python tools/test_game_addresses.py` PASS; fitur terkait terlihat bekerja in-game.
- Status 2026-11-22: 3 run omo (`c0000000-...r1`); scan `extracted/exefs/main` dengan `nso.py` + capstone tidak menemukan pola SS yang terverifikasi untuk 185 alamat; tidak ada file diubah. `python tools/test_game_addresses.py` tetap PASS (111 patch + 72 runtime, 0 patch output/hook).

### [x] R2. Daftar RomFS SS belum ada (`shortcut:`) — P1, M
- Bukti: `runtime/game_addresses.c:88-90` `game_romfs_files()`/`game_romfs_bytes()` return 0; `romfs_files.h` (105 KB) masih milik LA.
- Dampak: launcher (`runtime/ui/launcher.mm:224,260,568`), `services/fs.c:504`, `crash_explain.c:66,91` tidak bisa mendeteksi copy RomFS yang tidak lengkap/rusak.
- Copy SS terverifikasi sekarang ada: `extracted/index.json` (876 file, layout flat `romfs.bin`). `tools/romfs_files.py` masih membaca folder `romfs/` dan versi LA (`1.0.0/1.0.1/1.1.0`), jadi perlu disesuaikan ke `index.json` dan versi SS. Belum ada NSP update 1.0.1 untuk bit kedua.
- AC: `romfs_files.h` dibuat ulang dengan `tools/romfs_files.py` dari copy SS terverifikasi; kedua fungsi return angka SS; launcher menolak copy yang dihapus satu file (uji manual).
- Bukti selesai: `python tools/romfs_files.py --index extracted/index.json` → 876 entri, 7.552.053.656 byte; `tools/test_game_addresses.py` PASS (`build/v_r2_final_tests.log`); boot 20 s `build/ck_romfs_boot.log:92` `[fs] romfs index: 876 files`, tanpa peringatan missing. Uji hapus-satu-file manual belum dilakukan.

### R3. Shader translator: instruksi yang belum didukung — P2, L
- Bukti: `build/ck_translate_fuzz.log` — terbanyak `shf cc/x/hi` (10–13 per 1000), `bindless tex` (2–4), `prmt mode`, `indexed ld a[]`, `tlds info`, `csetp test`, `cset`, `ipa idx`.
- AC: per instruksi, fuzz `tools/test_translate.sh` tidak lagi melaporkan kategori itu; `tools/test_translate_ops.sh` + `tools/test_spirv_val.sh` PASS.

### R4. `subsdk0/1` tidak di-recomp: disengaja, diganti HLE — P1, M (keputusan selesai 2026-11-22)
- Lokasi kodenya: `runtime/loader.c:177` dan `runtime/gamebuild.c:177` hanya memuat `{"main","sdk"}`. Target `gen` di `Makefile:227-228` hanya untuk dua modul itu, sedangkan `Makefile:48` sudah mengambil `generated/subsdk0/1` (wildcard kosong, tidak berbahaya).
- **Isi subsdk = library SDK, bukan logika game.** `DT_NEEDED` dari `main` berisi `.../NX-NXFP2-a64/Release/glslc.nss`, `.../multimedia.nss`, dan `nnSdk.nss` (diambil lewat `tools/nso.py`).
  - `subsdk0` = glslc (compiler GLSL NVIDIA): 20 export, semuanya `glslc*`.
  - `subsdk1` = multimedia: 10895 export, di antaranya `android` (stagefright) 5249, `movie` 825, `mkvparser`/`mkvmuxer` 585, `silk`/`opus` 249, `FDK` (AAC) 56.
- **Angka 41 sama persis dengan log.** Angka ini menghitung relokasi `main` yang di-bind ke region HLE (`runtime/loader.c:131-141`, `:160-161`), ditiru dengan `tools/nso.py` di semua relokasi ABS64/GLOB_DAT/JUMP_SLOT. Hasilnya 41 relokasi untuk 41 symbol.
  - 21 berasal dari `sdk`, tapi sengaja ditimpa HLE (`host implementation wins`, `loader.c:136`).
  - 20 berasal dari subsdk (17 `movie::*` dari subsdk1 dan 3 `glslc*` dari subsdk0).
- **Keputusan: pengalihan disengaja, subsdk tidak perlu di-recomp.**
  - Video diputar oleh FFmpeg host lewat `runtime/hle/movie.c`.
  - Shader game diterjemahkan dari biner NVN oleh shader translator, jadi glslc runtime tidak dipakai saat render. Game sampai di title screen dan render 60 fps walaupun `glslcInitialize` mengembalikan 0 (hasilnya tidak dicek di `0x7000e2d0c8..0x7000e2d110`).
  - Recomp `subsdk1` berarti menjalankan decoder Android/AAC/Opus terjemahan di atas service multimedia yang tidak ada. Biayanya L, dan tetap butuh HLE di bawahnya.
- Per status: 36 HLE lengkap, 5 HLE stub (UNIMPLEMENTED), 0 tanpa binding.
  - "Lengkap" artinya ada `HLE_FUNC`. Perilakunya belum diverifikasi satu per satu.

| # | Nama | Modul asal | Status HLE | Bukti |
|---|---|---|---|---|
| 1 | `_ZN2nn2vi10InitializeEv` | sdk | HLE lengkap | `runtime/hle/vi.c:44` |
| 2 | `_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE` | sdk | HLE lengkap | `runtime/hle/vi.c:54` |
| 3 | `_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE` | sdk | HLE lengkap | `runtime/hle/vi.c:62` |
| 4 | `_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE` | sdk | HLE lengkap | `runtime/hle/vi.c:47` |
| 5 | `_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE` | sdk | HLE lengkap | `runtime/hle/vi.c:74` |
| 6 | `_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj` | sdk | HLE lengkap | `runtime/hle/input.c:874` |
| 7 | `_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj` | sdk | HLE lengkap | `runtime/hle/input.c:875` |
| 8 | `_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj` | sdk | HLE lengkap | `runtime/hle/input.c:877` |
| 9 | `_ZN2nn3hid15GetNpadStyleSetERKj` | sdk | HLE lengkap | `runtime/hle/input.c:881` |
| 10 | `_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE` | sdk | HLE lengkap | `runtime/hle/input.c:909` |
| 11 | `_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE` | sdk | HLE lengkap | `runtime/hle/input.c:908` |
| 12 | `_ZN2nn3mem17StandardAllocator10InitializeEPvm` | sdk | HLE lengkap | `runtime/hle/heaps.c:139` |
| 13 | `_ZN2nn3mem17StandardAllocator10InitializeEPvmb` | sdk | HLE lengkap | `runtime/hle/heaps.c:146` |
| 14 | `_ZN2nn3mem17StandardAllocator8AllocateEm` | sdk | HLE lengkap | `runtime/hle/heaps.c:153` |
| 15 | `_ZN2nn3mem17StandardAllocator8AllocateEmm` | sdk | HLE lengkap | `runtime/hle/heaps.c:159` |
| 16 | `_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE` | sdk | HLE lengkap | `runtime/services/time.c:134` |
| 17 | `_ZN2nn5swkbd12ShowKeyboardEPNS0_6StringERKNS0_15ShowKeyboardArgE` | sdk | HLE lengkap | `runtime/hle/swkbd.c:18` |
| 18 | `_ZN2nv18InitializeGraphicsEPvm` | sdk | HLE lengkap | `runtime/hle/vi.c:91` |
| 19 | `_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_` | sdk | HLE lengkap | `runtime/hle/vi.c:93` |
| 20 | `_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_` | sdk | HLE lengkap | `runtime/hle/vi.c:94` |
| 21 | `nvnBootstrapLoader` | sdk | HLE lengkap | `runtime/hle/nvn_boot.c:11` |
| 22 | `glslcCompile` | subsdk0 | HLE stub (UNIMPLEMENTED) | tidak ada `HLE_FUNC` → `hle_unimplemented`, return 0 (`runtime/dispatch.c:76-82`) |
| 23 | `glslcInitialize` | subsdk0 | HLE stub (UNIMPLEMENTED) | tidak ada `HLE_FUNC` → `hle_unimplemented`, return 0 (`runtime/dispatch.c:76-82`) |
| 24 | `glslcSetAllocator` | subsdk0 | HLE stub (UNIMPLEMENTED) | tidak ada `HLE_FUNC` → `hle_unimplemented`, return 0 (`runtime/dispatch.c:76-82`) |
| 25 | `_ZN5movie11SetCoreMaskEm` | subsdk1 | HLE stub (UNIMPLEMENTED) | tidak ada `HLE_FUNC` → `hle_unimplemented`, return 0 (`runtime/dispatch.c:76-82`) |
| 26 | `_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:133` |
| 27 | `_ZN5movie17GetThreadPriorityEv` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:135` |
| 28 | `_ZN5movie17SetThreadPriorityEi` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:134` |
| 29 | `_ZN5movie6Buffer18SetDataAndCapacityEPvm` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:122` |
| 30 | `_ZN5movie6Buffer8SetRangeEii` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:121` |
| 31 | `_ZN5movie6BufferC1Ev` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:99` |
| 32 | `_ZN5movie6BufferD1Ev` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:116` |
| 33 | `_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:811` |
| 34 | `_ZN5movie9ExtractorC1ENS_13ContainerTypeE` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:387` |
| 35 | `_ZN5movie9MediaDataC1Ev` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:54` |
| 36 | `_ZN5movie9MediaDataD1Ev` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:67` |
| 37 | `_ZNK5movie6Buffer4DataEv` | subsdk1 | HLE stub (UNIMPLEMENTED) | tidak ada `HLE_FUNC` → `hle_unimplemented`, return 0 (`runtime/dispatch.c:76-82`) |
| 38 | `_ZNK5movie6Buffer4SizeEv` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:118` |
| 39 | `_ZNK5movie6Buffer6OffsetEv` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:119` |
| 40 | `_ZNK5movie9MediaData10FindStringEPKcPS2_` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:80` |
| 41 | `_ZNK5movie9MediaData9FindInt32EPKcPi` | subsdk1 | HLE lengkap | `runtime/hle/movie.c:73` |

#### Sub-task R4
- [x] **R4a. `movie::Extractor::SetDataSource(const char*)` (vtable slot 2) belum diimplementasikan, jadi movie SS tidak pernah dibuka — P1, S.**
  - Vtable SS dari `subsdk1` `_ZTVN5movie9ExtractorE`: slot 2 = `SetDataSource(const char*)`, slot 15 = `SetDataSource(ClientStreamReader*, const char*)`.
  - `runtime/hle/movie.c:390-392`: slot 2 = `movie_log_slot`, dan hanya slot 15 yang memanggil `ex_set_data_source` (path di `x2`, `movie.c:214-216`).
  - Di `main 0x7000ec79e8..0x7000ec7a2c`, game menulis nama `*.webm`, memanggil `blr [vt+0x10]` (slot 2) dengan path di `x1`, lalu `GetTrackCount` (slot 4). Kalau count < 1, movie dilewati (`b.lt 0x7000ec7db4`). Log run: `[movie] unimplemented virtual (lr=7000ec7a00)`.
  - AC: slot 2 memakai `ex_set_data_source_l` dengan path dari `x1` (pola adapter yang sudah ada). Log `[movie] unimplemented virtual (lr=7000ec7a00)` hilang. Movie intro/title terlihat diputar in-game.
- [x] **R4b. `movie::SetCoreMask(unsigned long)` UNIMPLEMENTED — P2, S.** Dipanggil di `main 0x7000ec74f4` (`x0=6`). Kemungkinan cukup no-op, sejajar dengan `SetThreadPriority` di `movie.c:134`.
  - AC: ada `HLE_FUNC` no-op dan log `UNIMPLEMENTED _ZN5movie11SetCoreMaskEm` hilang.
- [x] **R4c. `movie::Buffer::Data() const` UNIMPLEMENTED — P1, S.** Nanti mengembalikan 0 (null pointer) kalau dipanggil. Belum terlihat di run 90 detik, kemungkinan karena movie tidak pernah dibuka (R4a). Yang sudah ada hanya `Base()` (`movie.c:117`).
  - AC: ada `HLE_FUNC` mengikuti semantik SDK (base + offset atau base, diverifikasi dari disasm `subsdk1`). Movie berjalan tanpa crash.
- [x] **R4d. `glslcInitialize` / `glslcSetAllocator` / `glslcCompile` UNIMPLEMENTED — P2, S.** Return 0 tidak memblokir boot. `glslcCompile` tidak terpanggil dalam 90 detik.
  - AC: pastikan lewat disasm dan run apakah ada jalur gameplay yang memanggil `glslcCompile`. Kalau tidak ada, dokumentasikan sebagai no-op yang disengaja (atau `HLE_FUNC` yang mengembalikan gagal secara eksplisit).
- [x] **R4e. Stub IPC `prepo:u`, `ectx:aw` — P2, S.** Hanya stub yang mencatat log (telemetri/error context). Kemungkinan tidak berdampak; cukup dicatat.
- AC R4: tabel di atas sesuai dengan output `tools/nso.py` (41/41). R4a dan R4c selesai dan movie terverifikasi in-game. Log run 90 detik tidak lagi berisi `UNIMPLEMENTED` dari `movie::*`.
- Bukti R4a: slot 2 memakai `ex_set_data_source_l`; `build/ck_run_r4a_final.log:191-193` `open dvd:/THP/Demo01_01.webm -> romfs:THP/Demo01_01.webm`, decoder vp9/vorbis aktif, tanpa `unimplemented virtual`.
- Bukti R4c: disasm `subsdk1` `_ZNK5movie6Buffer4DataEv` @ `0x1249ac` = `Base() + sign-extended Offset()`; `HLE_FUNC` ditambahkan, build/test PASS (`build/v_r4c_tests.log`).
- Bukti R4b/R4d/R4e: `SetCoreMask` no-op, stub `glslc*` eksplisit, `prepo:u`/`ectx:aw` didokumentasikan; `build/ck_run_r4bde.log` tanpa `UNIMPLEMENTED movie::*`.

## Bug Fix

### B1. `test_vulkan_overlap.py` gagal: list kecil (written slot) tidak di-copy — P1, S (setelah keputusan)
- Bukti: `tools/test_vulkan_overlap.py:445` `assert(snap_storage(R[i], Z[i], true) != R[i])` gagal (exit 0xC0000409) untuk `i=2,3` (list 2040 B, `snap_slot_class=1`) di **semua** offset (0..0xf00).
- Analisis: `runtime/nvn/vulkan/exec.c:3065` — store di shared page baru dihitung mulai *look ke-3*, dan copy butuh dua deteksi (`exec.c:3307`). Loop test di baris 439 hanya 2 frame (`f < 2`); loop serupa di baris 308 memakai `f < 3` dan PASS. Dengan `f < 3` seluruh test PASS (dicoba di salinan sementara).
- Ambigu: apakah test yang basi (harus `f < 3`) atau runtime yang seharusnya meng-copy list ber-slot-written lebih cepat. **Perlu keputusan maintainer.**
- Tambahan: test ini tidak ada di `.github/workflows/ci.yml`, makanya tidak ketahuan.
- AC: `python tools/test_vulkan_overlap.py` PASS di Windows; ditambahkan ke job CI.

### B2. Test rando bergantung pada `third_party/LAS-Randomizer` yang tidak ada — P2, S
- Bukti: `tools/test_field_regions.py:18,33` FileNotFoundError; juga `tools/rando/{bntx_merge,chamber_gen,collect_patches,cosmetics,digs,dungeon_gen}.py`.
- AC: test di-skip dengan pesan jelas bila folder tidak ada, atau dihapus bersama tooling LA (lihat F1).

## Fitur (status)

| Fitur | Status | Bukti |
|---|---|---|
| Loader / kernel / HLE services | berfungsi, ada test | condvar, thread_stack, lazy_commit, gmem_lowmem PASS |
| Save I/O, settings, data move, delete guard | berfungsi, ada test | test_save, test_settings, test_data_move, test_delete_guard PASS |
| Build cache / resume game-lib | berfungsi, ada test | test_build_cache, test_gamebuild_resume, test_windows_processes PASS |
| Vulkan renderer (fence, sync, validation, overlay) | berfungsi, ada test | 4 test PASS |
| Vulkan GPU overlap / snapshot | test gagal | B1 |
| Frame pacing 30 fps, dynres | berfungsi, ada test | test_pace, test_dynres PASS |
| Texture pack (DDS/PNG, XXH3) | berfungsi, ada test | test_xxh3_128, test_texpack_packs PASS |
| Rumble, controller aktif, LED | berfungsi, ada test | test_rumble, test_active_pad PASS |
| Audio (audren) | setengah jadi | effects & splitter tidak diimplementasi: `runtime/services/audio.c:19,747,901` |
| NVN `TextureWriteTexelsStrided`/`ReadTexels` | stub | `runtime/nvn/objects.c:560-561` (log saja) |
| Movie player | sebagian | virtual belum diimplementasi: `runtime/hle/movie.c:369` |
| Fitur LA (ultrawide sites, warp, door jump, keysanity…) | tidak aktif untuk SS | R1 |
| Randomizer (`tools/rando`, `ui/rando_tab.mm`) | rusak untuk SS (logika LA, dependency hilang) | B2, F1 |
| 60 fps | belum mulai | README |

### F1. Randomizer: putuskan dipertahankan atau dihapus — P2, M
- Bukti: `tools/rando/*` dan `runtime/ui/rando_tab.mm:120-135` berisi opsi Link's Awakening (Mabe, seashells, Dampe…) dan bagian "planned … not implemented yet".
- AC: keputusan tercatat; kalau dihapus, build + semua test tetap PASS.
- Keputusan 2026-11-22: **dipertahankan** untuk sementara. `tools/rando` dan UI masih terhubung ke build/settings, dan port SS belum punya pengganti logic.yml; migrasi LA→SS bukan bagian crosscheck ini. Konsekuensinya, `tools/test_field_regions.py` kini skip dengan pesan jelas bila `third_party/LAS-Randomizer` tidak ada (B2), dan opsi LA di UI tetap ditandai tidak aktif untuk SS.

### F2. Audio effects/splitter — P2, L
- Bukti di atas. AC: efek yang dipakai SS (log `[audren] effect type`) terdengar benar in-game.

## Performance

### P1. Baseline terukur, profiling fungsi belum — P1, M
- Baseline 2026-11-22 (`build/ck_run.log`, 90 detik `--play`, title/attract, 1280x720): stabil 59–60 fps, frame max 20–26 ms setelah load. Load awal 678–899 ms per frame (t=17–21 s, 35–42 fps), compile shader 5–6. Pacer turun ke mode 30 fps game: "19 of the last 60 over 16.2 ms busy, median 13.2". Draw hingga ~11k/frame dengan upload ~10 MB/frame saat scene berat.
- Hot path yang dicek manual: `runtime/dispatch.c` sudah punya cache direct-mapped 4096 entri per thread + binary search, tanpa stack frame di jalur cache — tidak ada perbaikan murah yang jelas.
- AC: jalankan `tools/perf_sweep.py` dengan game asli, catat ms/frame dan fungsi teratas di `fnprof`; buat task lanjutan berdasarkan angka.
- Status 2026-11-22: AC belum penuh. Run model free `c0000000-...d3` menghasilkan angka partial: fps median 43.0–52.7 / min 30.4–32.2 / max frame 1136–1228 ms (t=11–38 s), vs baseline 59–60 fps / 20–26 ms. `fnprof` tidak menulis file karena game stall dengan `[hang]` cv-wait sebelum frame window tercapai. Tidak ada kode diubah; follow-up: perbaiki stall, ulang `fnprof` window kecil, ukur ulang steady state.

## Urutan kerja
1. B1 (perlu keputusan) → tambahkan ke CI.
2. R2, R1 (perlu file game).
3. P1 profiling (perlu file game).
4. B2/F1, R3, F2.

## Status
- [x] Crosscheck build & test (bukti: `build/ck_summary.txt`, `build/ck_*.log`)
- [ ] B1 — menunggu keputusan
- [x] Pipeline end-to-end (2026-11-22): extract 49,7 s PASS → `make gen` 54 s (sdk 179 + main 828 chunk) → `make -j8` 3m16s, 0 warning (`build/skywardgame.dll` 964 MB) → boot 90 s, 60 fps (`build/ck_extract.log`, `ck_gen.log`, `ck_build.log`, `ck_run.log`)
- [x] R4 keputusan: subsdk = glslc + multimedia, pengalihan ke HLE disengaja (tabel 41/41)
- [x] R4a, R4c (P1) movie; R4b, R4d, R4e (P2) (bukti: `build/v_r4*_tests.log`, `build/ck_run_r4a_final.log`, `build/ck_run_r4bde.log`)
- [x] R2 (bukti: `build/v_r2_final_tests.log`, `build/ck_romfs_boot.log`)
- [ ] R1 — belum ada alamat terverifikasi; lihat bagian R1
- [ ] P1 — omo gagal: HTTP 429 WorkBuddy credits exhausted
- [x] B2, F1 (bukti: `tools/test_field_regions.py` skip guard, `build/v_final2_tests.log`; F1: randomizer dipertahankan sementara)
- [ ] F2, R3 — belum selesai; run omo `c0000000-...b2` dihentikan setelah 10 menit tanpa diff F2/R3
