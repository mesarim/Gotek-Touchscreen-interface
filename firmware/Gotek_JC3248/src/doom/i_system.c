/* i_system.c — PrBoom system layer for the GTi (JC3248).
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  Copyright (C) 1999 by id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2006 by Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  ESP32 port Copyright 2016-2017 Espressif Systems (Shanghai) PTE LTD
 *  GTi adaptation 2026 OMEGAWARE.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version. See COPYING.GPL2.
 *
 *  GTi changes vs the Espressif port: the WAD partition is memory-mapped ONCE as a whole
 *  (the S3 MMU has room; the original ESP32 had to map lump by lump), I_Read advances the
 *  file offset like POSIX read(), and the IDF-5 partition/mmap API is used.
 */
#include "config.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>

#include "m_argv.h"
#include "lprintf.h"
#include "doomtype.h"
#include "doomdef.h"
#include "m_fixed.h"
#include "r_fps.h"
#include "i_system.h"
#include "i_joy.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_partition.h"

#define GTI_DOOM_PART_TYPE    0x40
#define GTI_DOOM_PART_SUBTYPE 0x06

int realtime=0;

void I_uSleep(unsigned long usecs)
{
  vTaskDelay(usecs/1000/portTICK_PERIOD_MS);
}

static unsigned long getMsTicks(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_usec/1000+tv.tv_sec*1000;
}

int I_GetTime_RealTime (void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * TICRATE + (tv.tv_usec * TICRATE) / 1000000);
}

const int displaytime=0;

fixed_t I_GetTimeFrac (void)
{
  unsigned long now;
  fixed_t frac;
  now = getMsTicks();
  if (tic_vars.step == 0)
    return FRACUNIT;
  frac = (fixed_t)((now - tic_vars.start + displaytime) * FRACUNIT / tic_vars.step);
  if (frac < 0) frac = 0;
  if (frac > FRACUNIT) frac = FRACUNIT;
  return frac;
}

void I_GetTime_SaveMS(void)
{
  if (!movement_smooth)
    return;
  tic_vars.start = getMsTicks();
  tic_vars.next = (unsigned int) ((tic_vars.start * tic_vars.msec + 1.0f) / tic_vars.msec);
  tic_vars.step = tic_vars.next - tic_vars.start;
}

unsigned long I_GetRandomTimeSeed(void)
{
  return (unsigned long)getMsTicks();
}

const char* I_GetVersionString(char* buf, size_t sz)
{
  snprintf(buf,sz,"%s v%s (http://prboom.sourceforge.net/) GTi",PACKAGE,VERSION);
  return buf;
}

const char* I_SigString(char* buf, size_t sz, int signum)
{
  if (sz) buf[0]=0;
  return buf;
}

// ── the WAD lives in the "doom" flash partition, mapped once ────────────────
static const uint8_t* s_wad = NULL;
static uint32_t s_wad_size = 0;
static esp_partition_mmap_handle_t s_wad_h;

static int wadMap(void)
{
  if (s_wad) return 1;
  const esp_partition_t* p = esp_partition_find_first(GTI_DOOM_PART_TYPE, GTI_DOOM_PART_SUBTYPE, NULL);
  if (!p) { lprintf(LO_ERROR, "I_Open: no doom partition\n"); return 0; }
  const void* a = NULL;
  if (esp_partition_mmap(p, 0, p->size, ESP_PARTITION_MMAP_DATA, &a, &s_wad_h) != ESP_OK || !a) {
    lprintf(LO_ERROR, "I_Open: mmap of doom partition failed\n");
    return 0;
  }
  s_wad = (const uint8_t*)a;
  s_wad_size = p->size;
  return 1;
}

// GTi lab13d: two "files". DOOM1.WAD is the flash partition; PRBOOM.WAD is PrBoom's own
// resource lumps (CRBRICK etc.), compiled into the firmware (gti_prboom_wad.c) because a
// stock doom1.wad does not carry them and PrBoom I_Errors without them.
extern const unsigned char gti_prboom_wad[];
extern const unsigned int  gti_prboom_wad_len;

typedef struct { int used; long offset; const uint8_t* base; uint32_t size; } FileDesc;
static FileDesc fds[16];

int I_Open(const char *wad, int flags) {
  int x;
  const uint8_t* base; uint32_t size;
  if (strcasecmp(wad, "DOOM1.WAD") == 0) {
    if (!wadMap()) return -1;
    base = s_wad; size = s_wad_size;
  } else if (strcasecmp(wad, "PRBOOM.WAD") == 0) {
    base = gti_prboom_wad; size = gti_prboom_wad_len;
  } else {
    lprintf(LO_INFO, "I_Open: open %s failed\n", wad);
    return -1;
  }
  for (x=3; x<16 && fds[x].used; x++) ;
  if (x>=16) return -1;
  fds[x].used=1; fds[x].offset=0; fds[x].base=base; fds[x].size=size;
  return x;
}

int I_Lseek(int ifd, off_t offset, int whence) {
  if (whence==SEEK_SET) fds[ifd].offset=offset;
  else if (whence==SEEK_CUR) fds[ifd].offset+=offset;
  else if (whence==SEEK_END) fds[ifd].offset=(long)fds[ifd].size+offset;
  return (int)fds[ifd].offset;
}

int I_Filelength(int ifd)
{
  return (int)fds[ifd].size;
}

void I_Close(int fd) {
  if (fd>=0 && fd<16) fds[fd].used=0;
}

void *I_Mmap(void *addr, size_t length, int prot, int flags, int ifd, off_t offset) {
  if (ifd<0 || ifd>=16 || !fds[ifd].base || (uint32_t)offset+length > fds[ifd].size) return NULL;
  return (void*)(fds[ifd].base + offset);
}

int I_Munmap(void *addr, size_t length) {
  return 0;   // the whole partition stays mapped for the life of the Doom boot
}

void I_Read(int ifd, void* vbuf, size_t sz)
{
  if (!fds[ifd].base || (uint32_t)fds[ifd].offset+sz > fds[ifd].size) {
    I_Error("I_Read: read past end of WAD");
    return;
  }
  memcpy(vbuf, fds[ifd].base + fds[ifd].offset, sz);
  fds[ifd].offset += sz;
}

const char *I_DoomExeDir(void)
{
  return "";
}

char* I_FindFile(const char* wfname, const char* ext)
{
  return NULL;
}

void I_SetAffinityMask(void)
{
}

/* access() comes from ESP-IDF's VFS; the engine only calls it for -file extras we never pass. */
