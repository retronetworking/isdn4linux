#ifdef MODULE
#	include <linux/module.h>
#endif
#include <stdio.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/isdnif.h>
#include "debug.h"
