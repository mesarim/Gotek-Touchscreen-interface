/* Emacs style mode select   -*- C++ -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  Provides a logical console output routine that allows what is
 *  output to console normally and when output is redirected to
 *  be controlled..
 *
 *-----------------------------------------------------------------------------*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifdef _MSC_VER
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "doomtype.h"
#include "lprintf.h"
#include "i_main.h"
#include "m_argv.h"

#include "rom/ets_sys.h"

int cons_error_mask = -1-LO_INFO; /* all but LO_INFO when redir'd */
int cons_output_mask = -1;        /* all output enabled */

/* cphipps - enlarged message buffer and made non-static
 * We still have to be careful here, this function can be called after exit
 */
#define MAX_MESSAGE_SIZE 2048


//Esp32 doesn't use the 2K-sized stack-allocated string but directly passes args to vprintf.
#if 1
/* GTi lab13c: also keep the last ~127 characters of console output in RTC memory
 * (g_doom_lastmsg, defined in the .ino) so a crash or I_Error can be reported on
 * the next GTi boot - the Doom boot has no SD card to log to. */
extern char g_doom_lastmsg[128];
int lprintf(OutputLevels pri, const char *s, ...) {
  char tmp[128]; int n; size_t cur, add;
  va_list v;
  va_start(v,s);
  vprintf(s,v);
  va_end(v);
  va_start(v,s);
  n=vsnprintf(tmp,sizeof(tmp),s,v);
  va_end(v);
  if (n>0) {
    char *p;
    for (p=tmp; *p; p++) if (*p=='\n'||*p=='\r'||*p=='\t') *p=' ';
    add=strlen(tmp);
    cur=strnlen(g_doom_lastmsg,127);
    if (cur+add>127) { size_t keep=127-add; memmove(g_doom_lastmsg,g_doom_lastmsg+cur-keep,keep); cur=keep; }
    memcpy(g_doom_lastmsg+cur,tmp,add); cur+=add;
    g_doom_lastmsg[cur]=0;
  }
  return 0;
}
#endif


#if 0
int lprintf(OutputLevels pri, const char *s, ...)
{
  int r=0;
//  char msg[MAX_MESSAGE_SIZE];
  int lvl=pri;

  va_list v;
  va_start(v,s);

#ifdef HAVE_VSNPRINTF
  vsnprintf(msg,sizeof(msg),s,v);         /* print message in buffer  */
#else
  vsprintf(msg,s,v);
#endif
  va_end(v);

//  r=ets_printf("%s",msg);           /* select output at console */

  return r;
}
#endif

/*
 * I_Error
 *
 * cphipps - moved out of i_* headers, to minimise source files that depend on
 * the low-level headers. All this does is print the error, then call the
 * low-level safe exit function.
 * killough 3/20/98: add const
 */

void I_Error(const char *error, ...)
{
  char errmsg[MAX_MESSAGE_SIZE];
  va_list argptr;
  va_start(argptr,error);
#ifdef HAVE_VSNPRINTF
  vsnprintf(errmsg,sizeof(errmsg),error,argptr);
#else
  vsprintf(errmsg,error,argptr);
#endif
  va_end(argptr);
  lprintf(LO_ERROR, "%s\n", errmsg);
#ifdef _MSC_VER
  if (!M_CheckParm ("-nodraw")) {
    //Init_ConsoleWin();
    MessageBox(con_hWnd,errmsg,"PrBoom",MB_OK | MB_TASKMODAL | MB_TOPMOST);
  }
#endif
  I_SafeExit(-1);
}
