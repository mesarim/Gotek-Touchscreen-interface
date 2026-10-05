/* i_video.c — PrBoom video + input layer for the GTi (JC3248).
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
 *  Doom renders 8-bit paletted 320x240 into an internal-RAM buffer. I_FinishUpdate hands it to
 *  the GTi (gti_doom_present, in the .ino), which scales it onto the panel framebuffer and
 *  flushes. Input is the touch screen, polled once per tic and turned into key events:
 *
 *    +--------+---------------------+--------+     (480x320 landscape canvas)
 *    | EXIT   |       MENU (Esc)    |        |
 *    |(hold 2s)                     |  FIRE  |
 *    |  D-PAD |   ENTER / YES       |        |
 *    | (left  |                     +--------+
 *    |  third)|   WEAPON            |  USE   |
 *    +--------+---------------------+--------+
 */
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include "m_argv.h"
#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "d_main.h"
#include "d_event.h"
#include "g_game.h"
#include "i_video.h"
#include "z_zone.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "lprintf.h"
#include "esp_heap_caps.h"

// ── bridge to the GTi sketch (defined extern "C" in Gotek_JC3248.ino) ─────────
extern void gti_doom_present(const unsigned char* src, const unsigned short* pal, int w, int h);
extern int  gti_doom_touch(int* x, int* y);   // 1 = finger down; x,y in the 480x320 landscape canvas
extern void gti_doom_exit(void);              // clears the Doom flag and reboots into the GTi
extern unsigned long gti_millis(void);

int use_fullscreen=0;
// joystick config variables the menu/config code references (touch is keyboard emulation)
int usejoystick=0;
int joyleft, joyright, joyup, joydown;
int use_doublebuffer=0;

// ── touch -> keys ────────────────────────────────────────────────────────────
enum { B_UP=1<<0, B_DOWN=1<<1, B_LEFT=1<<2, B_RIGHT=1<<3, B_FIRE=1<<4, B_USE=1<<5,
       B_ESC=1<<6, B_ENTER=1<<7, B_WEAPON=1<<8, B_YES=1<<9 };

// lab13e: one touch read per tic, shared by the game zones and the menu handler.
static int s_down, s_tx, s_ty;

// Top-left corner held 2 s leaves Doom (works in game and in menus). 1 = finger is in the corner.
static int exitCorner(void)
{
  static unsigned long exitSince=0;
  if (!s_down || !(s_tx<50 && s_ty<50)) { exitSince=0; return 0; }
  if (!exitSince) exitSince=gti_millis();
  else if (gti_millis()-exitSince>2000) gti_doom_exit();
  return 1;
}

static int touchMask(void)
{
  int x=s_tx, y=s_ty;
  if (!s_down || exitCorner()) return 0;
  if (x<160) {                                         // left third: virtual d-pad around (80,200)
    int m=0, dx=x-80, dy=y-200;
    if (dy<-22) m|=B_UP; else if (dy>22) m|=B_DOWN;
    if (dx<-22) m|=B_LEFT; else if (dx>22) m|=B_RIGHT;
    return m;
  }
  if (x>=320) return (y<160) ? B_FIRE : B_USE;         // right third: fire (top) / use (bottom)
  if (y<90)   return B_ESC;                            // middle: menu
  if (y<220)  return B_ENTER;                          //         enter (lab13e: no auto-'y')
  return B_WEAPON;                                     //         next weapon
}

static void postKey(int down, int key)
{
  event_t ev;
  ev.type = down ? ev_keydown : ev_keyup;
  ev.data1 = key; ev.data2 = 0; ev.data3 = 0;
  D_PostEvent(&ev);
}

extern boolean setup_active;                 // Boom setup screens keep the key-zone layout
extern void GTi_MenuTap(int tx, int ty);     // m_menu.c (lab13e)

void I_StartTic (void)
{
  static int old=0, wasDown=0, waitRelease=0;
  int now, ch, press;
  s_down=gti_doom_touch(&s_tx,&s_ty);
  press = s_down && !wasDown; wasDown = s_down;
  if (!s_down) waitRelease=0;
  if (menuactive && !setup_active) {
    now=0;                                   // let go of any held game keys first
    ch=old; if (ch) goto post;               // (falls through to the key-up posting below)
    if (!exitCorner() && press) { GTi_MenuTap(s_tx,s_ty); waitRelease=1; }
    return;
  }
  now = waitRelease ? 0 : touchMask();       // the tap that closed a menu doesn't also fire/move
  ch=now^old;
  if (!ch) return;
post:
  if (ch&B_UP)     postKey(now&B_UP,     key_up);
  if (ch&B_DOWN)   postKey(now&B_DOWN,   key_down);
  if (ch&B_LEFT)   postKey(now&B_LEFT,   key_left);
  if (ch&B_RIGHT)  postKey(now&B_RIGHT,  key_right);
  if (ch&B_FIRE)   postKey(now&B_FIRE,   key_fire);
  if (ch&B_USE)    postKey(now&B_USE,    key_use);
  if (ch&B_ESC)    postKey(now&B_ESC,    key_escape);
  if (ch&B_ENTER)  postKey(now&B_ENTER,  key_menu_enter);
  if (ch&B_YES)    postKey(now&B_YES,    'y');
  if (ch&B_WEAPON) postKey(now&B_WEAPON, key_weapontoggle);
  old=now;
}

static void I_InitInputs(void)
{
}

//////////////////////////////////////////////////////////////////////////////
// Graphics API

void I_ShutdownGraphics(void)
{
}

void I_UpdateNoBlit (void)
{
}

void I_StartFrame (void)
{
}

int I_StartDisplay(void)
{
  return true;
}

void I_EndDisplay(void)
{
}

static unsigned char *screenbuf;
static unsigned short lcdpal[256];     // byte-swapped RGB565, the GTi framebuffer's native order

void I_FinishUpdate (void)
{
  gti_doom_present(screenbuf, lcdpal, SCREENWIDTH, SCREENHEIGHT);
}

void I_SetPalette (int pal)
{
  int i, v;
  int pplump = W_GetNumForName("PLAYPAL");
  const byte * palette = W_CacheLumpNum(pplump);
  palette+=pal*(3*256);
  for (i=0; i<256 ; i++) {
    v=((palette[0]>>3)<<11)+((palette[1]>>2)<<5)+(palette[2]>>3);
    lcdpal[i]=(unsigned short)(((v>>8)&0xFF)|((v&0xFF)<<8));
    palette += 3;
  }
  W_UnlockLumpNum(pplump);
}

void I_PreInitGraphics(void)
{
  lprintf(LO_INFO, "preinitgfx");
  // 76.8 KB: internal RAM keeps the renderer's column writes fast; fall back to PSRAM.
  screenbuf=heap_caps_malloc(SCREENWIDTH*SCREENHEIGHT, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if (!screenbuf) screenbuf=heap_caps_malloc(SCREENWIDTH*SCREENHEIGHT, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if (!screenbuf) I_Error("I_PreInitGraphics: no memory for the screen");
}

void I_SetRes(void)
{
  int i;
  for (i=0; i<3; i++) {
    screens[i].width = SCREENWIDTH;
    screens[i].height = SCREENHEIGHT;
    screens[i].byte_pitch = SCREENPITCH;
    screens[i].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
    screens[i].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);
  }
  screens[4].width = SCREENWIDTH;
  screens[4].height = (ST_SCALED_HEIGHT+1);
  screens[4].byte_pitch = SCREENPITCH;
  screens[4].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
  screens[4].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);

  screens[0].not_on_heap=true;
  screens[0].data=screenbuf;

  lprintf(LO_INFO,"I_SetRes: Using resolution %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}

void I_InitGraphics(void)
{
  static int firsttime=1;
  if (firsttime) {
    firsttime = 0;
    atexit(I_ShutdownGraphics);
    lprintf(LO_INFO, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
    I_UpdateVideoMode();
    I_InitInputs();
  }
}

void I_UpdateVideoMode(void)
{
  lprintf(LO_INFO, "I_UpdateVideoMode: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
  V_InitMode(VID_MODE8);
  V_DestroyUnusedTrueColorPalettes();
  V_FreeScreens();
  I_SetRes();
  V_AllocScreens();
  R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);
}
