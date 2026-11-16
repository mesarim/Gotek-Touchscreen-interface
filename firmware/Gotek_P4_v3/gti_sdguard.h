// gti_sdguard.h — lab14g: a guard between FatFs and the SD card driver.
//
// Why (25 Sep 2026, the 26 GB bulk card): one read of the root-directory sector came back
// shifted by 52 BITS with no error reported. FatFs could not find gti.log in that copy,
// created a new entry in it and wrote the whole sector back - every real root entry, the
// library folder included, was gone. A shift by part of a byte is a transfer fault on the
// SD line (or in the card), not a software bug; the card itself passed H2testw.
//
// FatFs keeps ALL of its own bookkeeping - directory sectors, FAT sectors, FSINFO - in ONE
// buffer, fs->win. File DATA never goes through it (files use their own buffer, or the
// caller's). So a transfer whose buffer IS fs->win is metadata, and metadata has a shape we
// can check:
//   * a FAT32 FAT sector is 128 entries, each 0 (free), a cluster number, or end-of-chain;
//   * a directory sector is 16 slots, each empty, deleted, a long-name piece or a short entry
//     with printable name bytes, sane attribute bits and an in-range start cluster.
// A bit-shifted copy fails that almost everywhere.
//
// READ of metadata that fails the check: read again (up to 4 times). A later read that
// passes is used (the fault was in the transfer). If every read returns the SAME bytes, that
// is what the card really holds - pass it through (an odd but genuine sector must never make
// a folder unreadable). If the reads disagree and none passes: return an error, so FatFs
// fails that one operation instead of acting on garbage.
// WRITE of metadata: read the card's current copy first. If the buffer fails the check, or is
// a bit-SHIFTED copy of what's on the card (a bad read that slipped through the check, e.g. an
// almost-empty FAT sector), while the card's copy passes - refuse: that is exactly "replace a
// good directory with garbage". Every metadata write is
// read back and compared; a mismatch is rewritten once, then reported as an error.
// Everything else (file data, SD ACCESS raw sectors) passes straight through.
#pragma once
#include <stdint.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
// lab14o: a write whose buffer is in FLASH (a firmware constant) must be copied to RAM first. The SD
// driver sends a buffer by DMA when it is aligned and not PSRAM - it never checks that it is RAM - and
// DMA cannot read flash, so the card got garbage (every new CONFIG.TXT had its first 4 KB blank).
#if __has_include("esp_memory_utils.h")
#include "esp_memory_utils.h"
#define SG_NEEDS_BOUNCE(p) (!esp_ptr_dma_capable(p) && !esp_ptr_external_ram(p))
#else
#define SG_NEEDS_BOUNCE(p) false      // host tests
#endif

typedef DRESULT (*sg_lower_read_t)(BYTE* buf, LBA_t sector, UINT count);
typedef DRESULT (*sg_lower_write_t)(const BYTE* buf, LBA_t sector, UINT count);

typedef struct {
  FATFS* fs;                        // the mounted volume (its win buffer + geometry); NULL = pass-through
  sg_lower_read_t  lread;
  sg_lower_write_t lwrite;
  bool on;
  uint32_t meta_reads, meta_writes;           // metadata transfers seen
  uint32_t reads_fixed, reads_odd, reads_failed;
  uint32_t writes_refused, verify_fixed, verify_failed;
  uint32_t last_sector; int last_shift; uint8_t last_bad[16];   // the most recent bad read, for the log
  bool pending_report;                         // a new event since the caller last logged
} SdGuard;

static uint8_t sg_tmp[512] __attribute__((aligned(4)));
static uint8_t sg_tmp2[512] __attribute__((aligned(4)));
static uint8_t sg_bounce[512] __attribute__((aligned(4)));   // lab14o: RAM copy of a flash-resident write

static inline uint32_t sg_ld32(const uint8_t* p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline uint16_t sg_ld16(const uint8_t* p){ return (uint16_t)(p[0]|(p[1]<<8)); }

// 0 = reserved area (boot sector, FSINFO), 1 = FAT, 2 = data area (for fs->win: a directory)
static int sg_region(const FATFS* fs, LBA_t s){
  if (s < fs->fatbase) return 0;
  if (s < fs->fatbase + (LBA_t)fs->fsize * fs->n_fats) return 1;
  if (s >= fs->database) return 2;
  return 0;
}
static bool sg_fat_ok(const FATFS* fs, LBA_t s, const uint8_t* b){
  if (fs->fs_type != FS_FAT32 || fs->ssize != 512) return true;          // only FAT32 is checked
  uint32_t first = (uint32_t)((s - fs->fatbase) % fs->fsize) * 128u;      // index of this sector's first entry
  for (int i = 0; i < 128; i++){
    if (first + i < 2) continue;                                          // entries 0/1: media + flags
    if (first + i >= fs->n_fatent) break;                                 // past the last cluster: don't care
    uint32_t v = sg_ld32(b + i*4) & 0x0FFFFFFFu;
    if (v == 0 || (v >= 2 && v < fs->n_fatent) || v >= 0x0FFFFFF7u) continue;
    return false;
  }
  return true;
}
static bool sg_dir_ok(const FATFS* fs, const uint8_t* b){
  if (fs->ssize != 512) return true;
  for (int i = 0; i < 16; i++){
    const uint8_t* e = b + i*32;
    if (e[0] == 0x00) break;                                              // end of directory
    if (e[0] == 0xE5) continue;                                           // deleted
    uint8_t a = e[11];
    if ((a & 0x3F) == 0x0F){                                              // long-name piece
      uint8_t ord = e[0] & 0x3F;
      if ((e[0] & 0x80) || ord < 1 || ord > 20 || e[12] != 0 || e[26] != 0 || e[27] != 0) return false;
      continue;
    }
    if (a & 0xC0) return false;
    for (int j = 0; j < 11; j++){ uint8_t c = e[j]; if (c < 0x20 && !(j == 0 && c == 0x05)) return false; }
    if (e[0] == 0x20) return false;
    uint32_t cl = ((uint32_t)sg_ld16(e + 20) << 16) | sg_ld16(e + 26);
    if (fs->fs_type != FS_FAT32) cl &= 0xFFFF;
    if (cl == 1 || cl >= fs->n_fatent) return false;
  }
  return true;
}
static bool sg_meta_ok(const FATFS* fs, LBA_t s, const uint8_t* b){
  int r = sg_region(fs, s);
  return r == 1 ? sg_fat_ok(fs, s, b) : r == 2 ? sg_dir_ok(fs, b) : true;
}
// How far (in bits, -64..64) is `bad` shifted against `good`? 0 = not a plain shift.
// Only meaningful when `good` has real content and `bad` mostly does NOT match it unshifted
// (a normal FatFs update changes a few entries and leaves most bytes where they were).
static int sg_shift(const uint8_t* good, const uint8_t* bad){
  int same = 0, busy = 0;
  for (int k = 0; k < 512; k++){ if (good[k] == bad[k]) same++; if (good[k] != 0 && good[k] != 0xFF) busy++; }
  if (same * 2 >= 512 || busy < 64) return 0;
  for (int sh = 1; sh <= 64; sh++){
    for (int dir = -1; dir <= 1; dir += 2){
      int match = 0, tot = 0;
      for (int k = 16; k < 496; k++){                                     // compare bytes away from the edges
        int bitpos = k*8 + dir*sh;                                        // bad[k] ~ good bits starting at bitpos
        int byte = bitpos >> 3, off = bitpos & 7;
        if (byte < 0 || byte + 1 >= 512) continue;
        uint8_t v = (uint8_t)(((good[byte] << off) | (good[byte+1] >> (8-off))) & 0xFF);
        tot++; if (v == bad[k]) match++;
      }
      if (tot && match * 10 >= tot * 9) return dir * sh;
    }
  }
  return 0;
}
static DRESULT sg_read(SdGuard* g, BYTE* buf, LBA_t s, UINT n){
  DRESULT r = g->lread(buf, s, n);
  FATFS* fs = g->fs;
  if (r != RES_OK || !g->on || !fs || n != 1 || buf != fs->win) return r;
  g->meta_reads++;
  if (sg_meta_ok(fs, s, buf)) return RES_OK;
  memcpy(sg_tmp2, buf, 512);                                              // the suspect copy
  bool allSame = true;
  for (int t = 0; t < 4; t++){
    if (g->lread(sg_tmp, s, 1) != RES_OK){ allSame = false; continue; }
    if (sg_meta_ok(fs, s, sg_tmp)){
      g->reads_fixed++; g->last_sector = (uint32_t)s; memcpy(g->last_bad, sg_tmp2, 16);
      g->last_shift = sg_shift(sg_tmp, sg_tmp2); g->pending_report = true;
      memcpy(buf, sg_tmp, 512); return RES_OK;
    }
    if (memcmp(sg_tmp, sg_tmp2, 512) != 0) allSame = false;
  }
  if (allSame){ g->reads_odd++; return RES_OK; }                          // what the card really holds
  g->reads_failed++; g->last_sector = (uint32_t)s; memcpy(g->last_bad, sg_tmp2, 16); g->last_shift = 0; g->pending_report = true;
  return RES_ERROR;
}
static DRESULT sg_write(SdGuard* g, const BYTE* buf, LBA_t s, UINT n){
  if (SG_NEEDS_BOUNCE(buf)){                                              // lab14o: flash -> RAM, one sector at a time
    for (UINT i = 0; i < n; i++){
      memcpy(sg_bounce, buf + (size_t)i*512, 512);
      DRESULT r = sg_write(g, sg_bounce, s + i, 1);
      if (r != RES_OK) return r;
    }
    return RES_OK;
  }
  FATFS* fs = g->fs;
  if (!g->on || !fs || n != 1 || buf != fs->win) return g->lwrite(buf, s, n);
  g->meta_writes++;
  // What is on the card now? A metadata write changes a few entries of it - it never replaces
  // it with a SHIFTED copy of itself (that is the bad-read-written-back failure), and never
  // replaces a valid sector with one that fails the shape check.
  if (g->lread(sg_tmp, s, 1) == RES_OK && memcmp(sg_tmp, buf, 512) != 0){
    bool cardOk = sg_meta_ok(fs, s, sg_tmp);
    int sh = cardOk ? sg_shift(sg_tmp, buf) : 0;
    if (cardOk && (sh != 0 || !sg_meta_ok(fs, s, buf))){
      g->writes_refused++; g->last_sector = (uint32_t)s; memcpy(g->last_bad, buf, 16);
      g->last_shift = sh; g->pending_report = true;
      return RES_ERROR;                                                   // never overwrite good metadata with garbage
    }
  }
  for (int t = 0; t < 2; t++){
    DRESULT r = g->lwrite(buf, s, 1);
    if (r != RES_OK) return r;
    if (g->lread(sg_tmp, s, 1) == RES_OK && memcmp(sg_tmp, buf, 512) == 0){ if (t) g->verify_fixed++; return RES_OK; }
  }
  g->verify_failed++; g->last_sector = (uint32_t)s; g->pending_report = true;
  return RES_ERROR;
}
