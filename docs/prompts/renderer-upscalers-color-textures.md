# Prompt: FSR 3/4, DLSS, Vivid color filter, dan tekstur stabil di renderer Vulkan SkywardRecomp

Salin semua isi di bawah garis ke sesi agent baru di repo ini.

---

Kamu bekerja di repo SkywardRecomp (static recomp *Skyward Sword HD* → Windows native, renderer Vulkan 1.3). Tujuan:
kualitas gambar setara emulator fritchii (fork Eden) yang terpasang di `C:\Users\ramas\AppData\Roaming\fritchii`, tanpa
menyalin kode atau biner dari sana. Kerjakan bertahap. Satu fitur = satu commit, dan setiap fitur harus lolos AC-nya
sebelum lanjut.

## Kondisi sekarang (cek ulang sebelum mengubah apa pun)

- Post pass: `runtime/nvn/vulkan/exec.c` sekitar baris 6685-7070. Urutannya SMAA 1x, lalu FSR 1 (EASU + RCAS) hanya
  saat upscale, lalu CAS sharpen. Infrastrukturnya `post_layout_init`, `post_pipeline_kind` dan `post_image`: maks.
  3 input dan 32 byte push constant, output RGBA8 gamma-encoded.
- Settings: `runtime/settings.h:7-42`. Isinya `UPSCALE_BILINEAR/METALFX/FSR1`, `AA_OFF/AA_SMAA`, `sharpen`,
  `anisotropy`, `lod_bias`, dan `screen_filter` (Game Boy/GBC/Grayscale, `runtime/nvn/screen_filter.h`). Resolusi dan
  Dynamic ada di `runtime/nvn/output_scale.c`.
- Sampler: `runtime/nvn/vulkan/textures.c:1300-1312` (`mipLodBias`, anisotropy dipaksa hanya untuk tekstur ber-mip).
- Matriks kamera: `runtime/hle/widescreen.c:175`. View, projection dan view-projection ada di scene UBO, vertex slot 5.
  Ini titik untuk jitter dan motion vector.
- Shader translator: `runtime/nvn/maxwell/translate.c`. Instruksi yang belum didukung tercatat di `TASKS.md` R3:
  `shf cc/x/hi`, `bindless tex`, `prmt mode`, `indexed ld a[]`, `tlds info`, `csetp test`, `cset`, `ipa idx`.
- Default per GPU dan switch `LARECOMP_VK_*`: `docs/vulkan-gpu-defaults.md`.

## Referensi dari fritchii (hanya perilaku; jangan salin kode/biner)

- `fidelityfx\amd_fidelityfx_loader_vk.dll` dan `amd_fidelityfx_upscaler_vk.dll`: FSR 3.1/FSR 4 lewat FidelityFX
  API. Lisensinya melarang reverse engineering. Ambil biner dan header dari AMD FidelityFX SDK resmi (GPUOpen/GitHub),
  bukan dari folder ini.
- `streamline\` (kosong): DLSS lewat NVIDIA Streamline. Log-nya punya `Renderer.nvidia_dlss`, `nvidia_dlss_preset`,
  `nvidia_dlss_quality` dan `*_dlss_jitter` (jitter disuntik per game).
- `post_presets\Vivid.fxp`: chain
  `NaturalColors(Luma=1.15, Chroma=1.35) → Levels(InputBlack=0.02, InputWhite=0.98) → Sharpen(Amount=0.5)`.
  Terjemahkan matematikanya sendiri. Lisensi file `.fx` tidak diketahui, jadi jangan salin teksnya.
- `cache\shader\vulkan_static_pipelines.bin`: pipeline yang sudah dibangun dimuat saat boot (prewarm).

## Tugas

### 1. Input temporal (wajib sebelum FSR 3/4 dan DLSS)
Upscaler temporal butuh input yang tidak dihasilkan game. Bangun sekali dan pakai bersama untuk FSR 3, FSR 4 dan DLSS:
- **Jitter**: offset subpiksel Halton(2,3) ditambahkan ke projection di scene UBO (widescreen.c), hanya untuk pass 3D
  dunia. UI, shadow map, dan pass ortografis tidak dijitter. Panjang fase mengikuti rekomendasi SDK berdasarkan rasio
  render/display.
- **Motion vector**: compute pass yang me-reproject depth memakai view-projection frame ini dan frame sebelumnya
  (motion kamera). Objek yang bergerak sendiri (karakter, partikel, air) ditandai di reactive/transparency mask.
  Jangan mengarang vektor per objek.
- **Depth**: depth buffer game di resolusi render, ditambah info reversed-Z atau tidak.
- **HUD terpisah**: upscaler hanya menerima scene 3D. HUD/menu/teks dikomposit setelahnya di resolusi display.
  Kalau HUD tidak bisa dipisah, dokumentasikan alasannya dan pakai UI mask.
- **Reset history** saat cut kamera (lonjakan view matrix), loading, buka menu, movie, dan perubahan resolusi.
- **LOD bias**: saat upscaler temporal aktif, `mipLodBias += log2(render_w / display_w)` (textures.c:1306), supaya
  tekstur tidak buram dan tidak shimmer.

### 2. FSR 3.1 (semua vendor) dan FSR 4 (AMD RDNA 4)
- Integrasikan FidelityFX SDK (API `ffx*` dengan backend Vulkan). Muat DLL secara dinamis. Kalau DLL tidak ada,
  fallback ke FSR 1 yang sudah ada, tanpa crash.
- FSR 4 hanya dipilih bila loader melaporkannya tersedia (RDNA 4). Kalau tidak, pakai FSR 3.1. Tulis ke log mana
  yang terpilih dan alasannya.
- Mode kualitas: Native AA, Quality, Balanced, Performance, Ultra Performance. Slider sharpening dipetakan ke RCAS
  milik SDK, dan CAS lama dimatikan saat FSR 3/4 aktif.
- Frame generation FSR 3 tidak termasuk scope ini (pacing 30/60 fps masih rapuh, lihat README). Sebutkan sebagai
  follow-up.

### 3. DLSS (NVIDIA RTX)
- Lewat NVIDIA Streamline atau NGX SDK Vulkan, dengan input yang sama dari langkah 1. Deteksi GPU RTX dan dukungan
  driver saat runtime. `nvngx_dlss.dll` hanya ikut di zip rilis, tidak di-commit ke git. Perbarui
  `THIRD_PARTY_NOTICES.md` dan `docs/RELEASING.md`.
- Preset (Auto, J, K, …) dan mode kualitas termasuk DLAA. Di non-NVIDIA, opsi ini disembunyikan atau di-grey-out.

### 4. Color filter "Vivid"
- Tambahkan `color_filter` di settings (Off, Vivid, plus Natural/Cinematic bila murah) di tab Video, terpisah dari
  `screen_filter`.
- Satu fragment pass gabungan di infrastruktur post yang ada, setelah upscaler dan sebelum HUD:
  luma ×1.15 dan chroma ×1.35 di ruang perseptual (Oklab/LCh, bukan saturasi RGB mentah, supaya hue tidak bergeser),
  levels black 0.02 / white 0.98, lalu sharpen ringan 0.5. Sharpen dilewati bila RCAS/CAS sudah aktif, agar tidak
  dobel.
- Parameter masuk lewat push constant (≤32 byte) supaya preset lain tidak butuh pipeline baru.

### 5. Tekstur tidak hilang/goyang (shader dan Vulkan)
Ukur dulu, baru perbaiki. Cari penyebab per gejala:
- **Hilang**: draw yang di-skip karena shader gagal diterjemahkan (R3). Tambahkan counter
  `[vk] draws skipped: translation failure N/s` dan kumpulkan instruksi penyebabnya di rute uji. Implementasikan
  instruksinya di translate.c sesuai urutan frekuensi. Validasi dengan `tools/test_translate.sh`,
  `tools/test_translate_ops.sh` dan `tools/test_spirv_val.sh`.
- **Pop-in/kedip**: draw yang dilewati selama pipeline masih compile. Prewarm pipeline dari cache disk saat boot atau
  layar loading, seperti `vulkan_static_pipelines.bin` di fritchii. Ukur `compiles pending` di log `[hitch]`.
- **Goyang/shimmer**: aliasing mip dan LOD bias yang salah, terutama saat upscale. Gunakan bias dari langkah 1 dan
  anisotropy 16x untuk tekstur ber-mip. Cek juga texture cache yang membaca data CPU setengah tertulis (hazard
  upload/barrier di `textures.c` dan `record.c`).
- Gunakan RenderDoc (`runtime/nvn/vulkan/rdoc.c`) untuk satu frame yang bermasalah sebelum dan sesudah fix, dan
  sertakan buktinya di PR.

## Aturan

- Semua fitur baru punya switch `LARECOMP_VK_*` dua arah (0/1), tercatat di log, dan bila ada default per GPU, dapat
  baris di `docs/vulkan-gpu-defaults.md`.
- Tidak boleh ada regresi di jalur lama: tanpa DLL eksternal, build tetap jalan dan perilaku default (FSR 1/SMAA)
  sama persis.
- Linux/macOS (CI) tetap build. DLSS/FSR 3/4 cukup Windows + Linux. Di macOS tetap MetalFX.
- Shader cache yang key-nya teks shader akan rebuild sekali. Itu wajar, sebutkan di catatan rilis.
- Jangan commit file game, key, `generated/`, `extracted/` atau biner SDK.

## Acceptance criteria (bukti log/screenshot wajib)

1. Rute uji 10 menit di Skyloft (terbang dan darat) dengan FSR 3 Quality, FSR 4 (bila RDNA 4) dan DLSS Quality
   (bila RTX): tidak ada ghosting berat pada Link dan rambut, tidak ada smearing HUD, dan history reset di setiap cut.
   Lampirkan screenshot berdampingan dengan native dan FSR 1.
2. `draws skipped: translation failure` = 0 di rute uji. Fuzz `tools/test_translate.sh` tidak lagi melaporkan
   kategori yang dikerjakan.
3. Setelah prewarm, `compiles pending` = 0 setelah 30 detik pertama di area yang sudah pernah dikunjungi.
4. Vivid: perbandingan screenshot Off/Vivid. Hue kulit dan langit tidak bergeser (cek ΔE di beberapa sampel), dan
   tidak ada clipping parah di putih.
5. Biaya GPU per frame dari log `[post]`/`LARECOMP_VK_PROF` dicatat per mode di PR. Target FSR 3 Quality 1440p→4K
   ≤ 1,5 ms di GPU kelas RTX 3060 / RX 6700.
6. Semua test yang ada tetap PASS di CI (Windows, Linux, macOS).

## Urutan kerja

1 (input temporal) → 5 (tekstur hilang/kedip, karena memengaruhi kualitas semua upscaler) → 2 (FSR 3/4) →
3 (DLSS) → 4 (Vivid, bisa dikerjakan paralel karena independen).

## Status (akhir sesi langkah 1)

Langkah 1 sebagian selesai. Belum di-commit.

- Selesai: `runtime/nvn/temporal.c` dan `temporal.h` (Halton, jumlah fase, jitter kamera, rumus motion vector, deteksi cut
  kamera). Tes game-free `tools/test_temporal.py` masuk langkah CI Linux dan lolos lokal di Windows. `make -j8 runtime-objs`
  (MSYS2 UCRT64) lolos; `temporal.c`, `exec.c`, `shaders.c` dan `textures.c` tanpa warning (`device.c:957` warning lama).
- Selesai, default off: `LARECOMP_VK_JITTER=1` menjitter salinan UBO kamera per draw di `cbuf_info`
  (`runtime/nvn/vulkan/exec.c`), bukan di `widescreen.c` seperti rencana awal. Salinan per draw, jadi cache bersama
  tidak berubah. Lebar output dicatat di `device.c`. Switch ada di `docs/debug-hooks.md`.
- Selesai, default off: `LARECOMP_VK_MOTION=1` menangkap kamera dan depth dari draw dunia terakhir, lalu menulis motion
  vector (piksel, previous minus current) ke post image 5 lewat `motion_run`. History direset di frame pertama, saat tidak
  ada draw dunia, saat depth di-clear setelah draw dunia, saat ukuran, viewport atau rentang depth berubah, saat cut kamera,
  atau saat kamera singular. Belum ada konsumen, jadi gambar tidak berubah. `motion.frag` lolos kompilasi shaderc
  (Vulkan 1.3, 814 word SPIR-V). Shader yang gagal membuat `vk_builtin_shader` berhenti dengan FATAL; hanya switch ini
  yang memanggilnya.
- Belum: pemisahan HUD dan UI mask; LOD bias (`textures.c` ~1306) sengaja belum disambungkan, karena bias negatif tanpa
  upscaler yang mengakumulasi frame menambah shimmer; backend Metal belum; link dan rute uji in-game belum; pass shadow
  yang memakai UBO kamera yang sama ikut dijitter (belum dipisahkan).
- Belum terverifikasi (butuh game atau RenderDoc): tanda sumbu Y jitter dan motion; apakah `st.rt.color[0]` sama dengan
  gambar yang diproses post; apakah UBO kamera minimal 320 byte di game; pipeline `motion.frag` di driver GPU; resolusi
  dinamis (motion hanya mencakup alokasi, bukan isi); sampler depth di driver.
