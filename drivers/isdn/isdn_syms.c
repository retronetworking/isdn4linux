#include <linux/module.h>
#include <linux/version.h>

#ifndef __GENKSYMS__      /* Don't want genksyms report unneeded structs */
#include <linux/isdn.h>
#endif
#include "isdn_common.h"

#if (LINUX_VERSION_CODE < 0x020111)
static int has_exported

static struct symbol_table isdn_syms = {
#include <linux/symtab_begin.h>
        X(register_isdn),
#include <linux/symtab_end.h>
};

void
isdn_export_syms(void)
{
	if (has_exported)
		return;
        register_symtab(&isdn_syms);
        has_exported = 1;
}

#else

EXPORT_SYMBOL(register_isdn);

#endif
