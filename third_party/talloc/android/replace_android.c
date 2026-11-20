/*
 * The only lib/replace fallback talloc.c needs on bionic: memset_s()
 * (C11 Annex K) is not provided, so replace.h maps it to rep_memset_s.
 * Same semantics as rep_memset_s() in lib/replace/replace.c (talloc
 * 2.4.4, LGPL-3.0-or-later); compiling that whole file would require
 * the full waf/libreplace configure results.
 */
#include "replace.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#ifndef RSIZE_MAX
#define RSIZE_MAX (SIZE_MAX >> 1)
#endif

int rep_memset_s(void *dest, size_t destsz, int ch, size_t count)
{
	if (dest == NULL) {
		return EINVAL;
	}

	if (destsz > RSIZE_MAX ||
	    count > RSIZE_MAX ||
	    count > destsz) {
		return ERANGE;
	}

	memset(dest, ch, count);
	/* Keep the compiler from eliding the store (llvm bug 15495). */
	__asm__ volatile("" : : "r"(dest) : "memory");

	return 0;
}
