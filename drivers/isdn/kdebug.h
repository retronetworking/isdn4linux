/* $Id$

 * Debugging utilities for Linux
 *
 * For debugging, include this file. If included more than once
 * in a multi-module driver, exactly one include-statement should
 * be preceeded by a #define KDBUG_DEF.
 *
 * What does this?:
 *   Main purpose is to realize a way of logging, even if interrupts
 *   are disabled. To work on virtual-screens with a screen-width other
 *   than 80, change the LINEWIDTH below.#
 *
 *   For use with modules, you have to export screen_pos and fg_console
 *   from console.c
 *
 * Functionality:
 *
 *  -If you are defined CLI_DEBUG
 *   cli(), sti() and restore_flags() are redefined. Every time, cli()
 *   is called, the filename and linenumber of the caller is printed
 *   on the console, preceeded by a "!". Once interrupts are enabled
 *   again, - either with sti() or restore_flags() - the exclamation
 *   mark is replaced by a blank.
 *
 *  -Wrapper-Macro for function calls. CTR(myfunc(a,b,c)) prints
 *   "Enter myfunc(...)", then executes the call, then prints
 *   "Leave myfunc(...)". Screenposition is 40,0.
 *
 *  -Generic counter macros. DBGCNTDEF(n,x,y); Defines counter nr. n
 *   to be shown at screen-position x,y. DBGCNTINC(n); increments
 *   counter n.
 *
 *  -An "i-am-alive" rotating bar. wheel(x,y) rotates a bar at
 *   position x,y.
 *
 * $Log$
 * Revision 1.2  1997/02/03 23:33:22  fritz
 * Reformatted according CodingStyle
 *
 * Revision 1.1  1996/04/30 09:22:55  fritz
 * Taken under CVS-control.
 *
 *
 */

#ifndef _kdebug_h_
#define _kdebug_h_

#include <asm/system.h>
#include <asm/io.h>

#define LINEWIDTH 80

extern unsigned short *screen_pos(int, int, int);
extern int fg_console;

#ifdef KDEBUG_DEF
unsigned short *scrpos;
int dbg_cnt_v[10];
int dbg_cnt_x[10];
int dbg_cnt_y[10];
char clibuf[256] = "\0";
#else
extern unsigned short *scrpos;
extern int dbg_cnt_v[10];
extern int dbg_cnt_x[10];
extern int dbg_cnt_y[10];
extern char clibuf[256];
#endif

static __inline__ void
put_ch(const char c)
{
	writew(0x0700 + c, scrpos++);
}

static __inline__ void
put_str(char *s)
{
	for (; *s; put_ch(*s++));
}

static __inline__ void
gotovid(int x, int y)
{
	scrpos = screen_pos(fg_console, (y * LINEWIDTH) + x, 1);
}

static __inline__ void
gput_str(char *s, int x, int y)
{
	gotovid(x, y);
	put_str(s);
}

static __inline__ void
gput_ch(char c, int x, int y)
{
	gotovid(x, y);
	put_ch(c);
}

static __inline__ void
wheel(int x, int y)
{
	gotovid(x, y);
	switch ((char) (readw(scrpos) & 0xff)) {
	case '/':
		writew(0x0700 + '-', scrpos);
		break;
	case '-':
		writew(0x0700 + '\\', scrpos);
		break;
	case '\\':
		writew(0x0700 + '|', scrpos);
		break;
	default:
		writew(0x0700 + '/', scrpos);
	}
}

#ifdef CLI_DEBUG

#ifndef __ASM_SYSTEM_H
#include <asm/system.h>
#endif

#ifdef __SMP__

extern void __global_cli(void);
extern void __global_sti(void);
extern void __global_restore_flags(unsigned long);
#define x__cli() __global_cli()
#define x__sti() __global_sti()
#define x__restore_flags(x) __global_restore_flags(x)

#else

#define x__cli() __cli()
#define x__sti() __sti()
#define x__restore_flags(x) __restore_flags(x)

#endif

#undef cli
#define cli() { \
  x__cli(); \
  sprintf(clibuf,"!%-20s %04d !",__BASE_FILE__,__LINE__); \
  gput_str(clibuf,0,0); \
}

#undef restore_flags
#define restore_flags(x) { \
  if (x & 0x200) gput_ch(' ',0,0); \
  x__restore_flags(x); \
}

#undef sti
#define sti() { \
  gput_ch(' ',0,0); \
  x__sti(); \
}
#endif				/* CLI_DEBUG */

#define CTR(x) { \
  gput_str("Enter " #x " ", 40, 0); \
  x; \
  gput_str("Leave " #x " ", 40, 0); \
}

#define DBGCNTDEF(n,x,y) { \
  char tmp[10]; \
  dbg_cnt_v[n] = 0; \
  dbg_cnt_x[n] = x; \
  dbg_cnt_y[n] = y; \
  sprintf(tmp,"%02d",0); \
  gput_str(tmp,x,y); \
}

#define DBGCNTINC(i) { \
  char tmp[10]; \
  dbg_cnt_v[i]++; \
  sprintf(tmp,"%02d",dbg_cnt_v[i]); \
  gput_str(tmp,dbg_cnt_x[i],dbg_cnt_y[i]); \
}

#endif
