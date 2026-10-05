/* gti_doom_alloc.c — GTi: Doom's large tables live on the heap, not in static RAM.
 *
 *  Part of the GTi PrBoom integration (GPL-2, see COPYING.GPL2).
 *
 *  The GTi and Doom never run in the same boot, but static arrays cost internal DRAM in
 *  EVERY boot. ~55 KB of PrBoom's biggest arrays were turned into pointers (definitions
 *  marked "GTi: heap" in the engine sources) and are allocated here, once, at the start of
 *  a Doom boot. The render-hot ones try internal RAM first; the rest go straight to PSRAM.
 *  calloc semantics = the zeroed state the old .bss arrays had.
 */
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomtype.h"
#include "doomstat.h"
#include "d_think.h"
#include "info.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_plane.h"
#include "r_things.h"
#include "hu_stuff.h"
#include "p_mobj.h"
#include "esp_heap_caps.h"   /* after the engine headers: doomtype.h defines its own true/false enum */

extern int  *viewangletox;           extern angle_t *xtoviewangle;
extern patchnum_t *hu_font, *hu_font2, *hu_fontk;
extern int  *gti_spanstart;
extern fixed_t *gti_cachedheight, *gti_cacheddistance, *gti_cachedxstep, *gti_cachedystep;
extern boolean *gti_gamekeydown;
extern byte *gti_byte_tempbuf;       extern unsigned short *gti_short_tempbuf; extern unsigned int *gti_int_tempbuf;
extern actionf_t *gti_deh_codeptr;
extern int  *gti_y_lookup;
extern mapthing_t *gti_itemrespawnque; extern int *gti_itemrespawntime;

#define GTI_NUMKEYS 512   /* g_game.c NUMKEYS */

static void* fast(size_t n)   /* internal first (render-hot), PSRAM fallback */
{
  void* p=heap_caps_calloc(1,n,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!p) p=heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return p;
}
static void* slow(size_t n)   /* PSRAM first - keep internal RAM for the screen + stack */
{
  void* p=heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!p) p=heap_caps_calloc(1,n,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  return p;
}

int gti_doom_alloc(void)
{
  /* render-hot, per column / per span */
  floorclip          = fast(MAX_SCREENWIDTH*sizeof(int));
  ceilingclip        = fast(MAX_SCREENWIDTH*sizeof(int));
  negonearray        = fast(MAX_SCREENWIDTH*sizeof(int));
  screenheightarray  = fast(MAX_SCREENWIDTH*sizeof(int));
  distscale          = fast(MAX_SCREENWIDTH*sizeof(fixed_t));
  yslope             = fast(MAX_SCREENHEIGHT*sizeof(fixed_t));
  gti_spanstart      = fast(MAX_SCREENHEIGHT*sizeof(int));
  gti_cachedheight   = fast(MAX_SCREENHEIGHT*sizeof(fixed_t));
  gti_cacheddistance = fast(MAX_SCREENHEIGHT*sizeof(fixed_t));
  gti_cachedxstep    = fast(MAX_SCREENHEIGHT*sizeof(fixed_t));
  gti_cachedystep    = fast(MAX_SCREENHEIGHT*sizeof(fixed_t));
  xtoviewangle       = fast((MAX_SCREENWIDTH+1)*sizeof(angle_t));
  gti_byte_tempbuf   = fast(MAX_SCREENHEIGHT*4*sizeof(byte));
  gti_short_tempbuf  = fast(MAX_SCREENHEIGHT*4*sizeof(unsigned short));
  gti_int_tempbuf    = fast(MAX_SCREENHEIGHT*4*sizeof(unsigned int));
  /* big / cold */
  viewangletox       = slow((FINEANGLES/2)*sizeof(int));
  hu_font            = slow(HU_FONTSIZE*sizeof(patchnum_t));
  hu_font2           = slow(HU_FONTSIZE*sizeof(patchnum_t));
  hu_fontk           = slow(HU_FONTSIZE*sizeof(patchnum_t));
  players            = slow(MAXPLAYERS*sizeof(player_t));
  gti_gamekeydown    = slow(GTI_NUMKEYS*sizeof(boolean));
  gti_deh_codeptr    = slow(NUMSTATES*sizeof(actionf_t));
  gti_y_lookup       = slow(MAX_SCREENWIDTH*sizeof(int));
  gti_itemrespawnque = slow(ITEMQUESIZE*sizeof(mapthing_t));
  gti_itemrespawntime= slow(ITEMQUESIZE*sizeof(int));
  return floorclip&&ceilingclip&&negonearray&&screenheightarray&&distscale&&yslope&&gti_spanstart
      && gti_cachedheight&&gti_cacheddistance&&gti_cachedxstep&&gti_cachedystep&&xtoviewangle
      && gti_byte_tempbuf&&gti_short_tempbuf&&gti_int_tempbuf&&viewangletox&&hu_font&&hu_font2
      && hu_fontk&&players&&gti_gamekeydown&&gti_deh_codeptr&&gti_y_lookup&&gti_itemrespawnque
      && gti_itemrespawntime;
}
