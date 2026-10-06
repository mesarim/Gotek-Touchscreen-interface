// gti_fatwalk.h — 5.9.41-lab14: read a file's first sector straight off the directory
// entry that f_readdir() just returned, without f_open(). lab14f: also remember where a
// file starts (fw_entry_clust) and open it later from that, with no name search (fw_open_at).
//
// Why: on FAT, opening a file BY NAME means searching its directory from the top. In a
// folder of 5,000 entries with long TOSEC names that is ~0.5 MB of directory data per
// open, so harvesting 2,500 .nfo blurbs from one letter folder re-reads the folder
// ~2,500 times (the 3-7 files/s seen on the 26 GB card). While we walk the directory
// the entry is already in FatFs's sector window, so its start cluster is right there:
// one sector read gets the blurb.
//
// Safety: nothing here writes, and the window is only trusted after the entry in it is
// checked against the FILINFO FatFs just produced (attribute, size, time, date and the
// 8.3 name). Any mismatch - the window moved to a FAT sector because the directory
// crossed a cluster, an odd code-page name, exFAT - returns false and the caller falls
// back to a normal open. Needs FatFs R0.15 (ESP-IDF 5.x) struct layout.
#pragma once
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include "ff.h"
#include "diskio.h"

// lab14f: where a file starts, noted during a walk (h = caller's hash of its lower-case path)
typedef struct FwLoc { uint64_t h; uint32_t sclust, size; uint16_t fdate, ftime; } FwLoc;

static inline uint16_t fw_ld16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t fw_ld32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// "NAME    EXT" -> "NAME.EXT" (0x05 lead byte stands for 0xE5)
static inline void fw_sfn(const uint8_t* e, char* out){
  int n = 0;
  for (int i = 0; i < 8; i++){ uint8_t c = e[i]; if (c == ' ') continue; if (i == 0 && c == 0x05) c = 0xE5; out[n++] = (char)c; }
  if (e[8] != ' '){ out[n++] = '.'; for (int i = 8; i < 11; i++){ if (e[i] == ' ') continue; out[n++] = (char)e[i]; } }
  out[n] = 0;
}
static inline bool fw_ieq(const char* a, const char* b){
  while (*a && *b){ if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}

// After a successful f_readdir(dp, fi) that returned a FILE, find that entry in FatFs's
// sector window and return its start cluster. The window is only trusted after the entry in it
// matches the FILINFO FatFs just produced (attribute, size, time, date, 8.3 name); any mismatch
// (the window moved to a FAT sector, exFAT, an odd code-page name) returns false.
// lab14f: split out of fw_read_head so the scan can also remember where each COVER starts.
static bool fw_entry_clust(FF_DIR* dp, const FILINFO* fi, uint32_t* clst_out){
  FATFS* fs = dp->obj.fs;
  if (!fs || fs->fs_type < FS_FAT12 || fs->fs_type > FS_FAT32) return false;   // FAT only (no exFAT)
  if (fi->fattrib & AM_DIR) return false;
  const uint32_t ss = fs->ssize;
  // f_readdir leaves dptr on the NEXT entry, unless it hit the end of the table
  // (then dir_next returned early and dptr still points at the entry we read).
  uint32_t ofs;
  if (dp->sect){ if (dp->dptr < 32) return false; ofs = dp->dptr - 32; }
  else ofs = dp->dptr;
  const uint8_t* e = fs->win + (ofs % ss);                 // dir_next never moves the window...
  if (e[0] == 0 || e[0] == 0xE5) return false;
  if ((e[11] & 0x3F) != fi->fattrib) return false;          // ...unless it crossed a cluster (FAT read):
  if (fw_ld32(e + 28) != (uint32_t)fi->fsize) return false; // these checks catch that
  if (fw_ld16(e + 22) != fi->ftime || fw_ld16(e + 24) != fi->fdate) return false;
  char sfn[13]; fw_sfn(e, sfn);
  if (!fw_ieq(sfn, fi->altname[0] ? fi->altname : fi->fname)) return false;
  uint32_t clst = fw_ld16(e + 26);
  if (fs->fs_type == FS_FAT32) clst |= (uint32_t)fw_ld16(e + 20) << 16;
  if (fi->fsize == 0){ *clst_out = 0; return true; }        // empty file: no cluster
  if (clst < 2 || clst >= fs->n_fatent) return false;
  *clst_out = clst;
  return true;
}

// After a successful f_readdir(dp, fi) that returned a FILE, read its first sector into
// sec (cap bytes, DMA-capable on the ESP32; the sector must fit). *n = bytes of it that
// belong to the file (min(size, sector)). Returns false if the shortcut can't be used safely.
static bool fw_read_head(FF_DIR* dp, const FILINFO* fi, uint8_t* sec, uint32_t cap, uint32_t* n){
  FATFS* fs = dp->obj.fs;
  if (!fs || fs->fs_type < FS_FAT12 || fs->fs_type > FS_FAT32) return false;
  if (fi->fattrib & AM_DIR) return false;
  if (fi->fsize == 0){ *n = 0; return true; }
  const uint32_t ss = fs->ssize;
  if (ss > cap) return false;
  uint32_t clst;
  if (!fw_entry_clust(dp, fi, &clst) || clst < 2) return false;
  LBA_t sect = fs->database + (LBA_t)fs->csize * (clst - 2);
  if (disk_read(fs->pdrv, sec, sect, 1) != RES_OK) return false;
  *n = ((uint32_t)fi->fsize < ss) ? (uint32_t)fi->fsize : ss;
  return true;
}

// lab14f: a read-only FIL for a file whose start cluster and size were recorded during a
// walk - what f_open() sets up after its by-name directory search, minus the search. f_read
// then follows the cluster chain as usual. `id` is the volume's mount id at walk time: if
// the card was remounted since, f_read's own validity check rejects the object (FR_INVALID_OBJECT)
// and the caller falls back to a normal open. Needs FF_FS_LOCK 0 and a fixed per-file buffer
// (ESP-IDF 5.x defaults); close with f_close() as usual.
static bool fw_open_at(FIL* fp, FATFS* fs, WORD id, uint32_t sclust, uint32_t size){
#if FF_FS_LOCK || FF_USE_DYN_BUFFER || FF_FS_EXFAT
  (void)fp; (void)fs; (void)id; (void)sclust; (void)size; return false;
#else
  if (!fs || fs->fs_type < FS_FAT12 || fs->fs_type > FS_FAT32 || fs->id != id) return false;
  if (size ? (sclust < 2 || sclust >= fs->n_fatent) : (sclust != 0)) return false;
  memset(fp, 0, sizeof(FIL));
  fp->obj.fs = fs; fp->obj.id = id; fp->obj.attr = AM_ARC;
  fp->obj.sclust = sclust; fp->obj.objsize = size;
  fp->flag = FA_READ;                                        // err, fptr, clust, sect = 0 (as f_open)
  return true;
#endif
}
