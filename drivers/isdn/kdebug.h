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
 * Functionality:
 *
 *  -cli(), sti() and restore_flags() are redefined. Every time, cli()
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
	*scrpos++ = 0x0700 + c;
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
	switch ((char) (*scrpos & 0xff)) {
		case '/':
			*scrpos = 0x700 + '-';
			break;
		case '-':
			*scrpos = 0x700 + '\\';
			break;
		case '\\':
			*scrpos = 0x700 + '|';
			break;
		default:
			*scrpos = 0x700 + '/';
	}
}

#undef cli
#define cli() { \
  __asm__ __volatile__ ("cli": : :"memory"); \
  sprintf(clibuf,"!%-20s %04d !",__BASE_FILE__,__LINE__); \
  gput_str(clibuf,0,0); \
}

#undef restore_flags
#define restore_flags(x) { \
  if (x & 0x200) gput_ch(' ',0,0); \
  __asm__ __volatile__("pushl %0 ; popfl": /* no output */ :"r" (x):"memory"); \
}

#undef sti
#define sti() { \
  gput_ch(' ',0,0); \
  __asm__ __volatile__ ("sti": : :"memory"); \
}

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
