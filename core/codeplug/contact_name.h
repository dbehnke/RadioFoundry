// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Shared display-name policy. Call from a task allowed to read the codeplug flash, never a drawing callback.
#ifndef RFOUNDRY_CONTACT_NAME_H
#define RFOUNDRY_CONTACT_NAME_H

#include <string.h>
#include "codeplug.h"
#include "dmr_iddb.h"

#if CONFIG_DMR
static inline bool contact_text_lookup(uint32_t id, bool group, char *out, size_t len)
{
	contact_t c;
	if (!len)
		return false;
	out[0] = '\0';
	if (id == 0 || id > 0xFFFFFFu)
		return false;
	if (codeplug_contact_find(id, group, &c)) {
		strncpy(out, c.name, len - 1);
		out[len - 1] = '\0';
	} else if (!group && dmr_iddb_lookup(id, out, len)) {
		return true;
	}
	return false;
}

static inline void contact_name_lookup(uint32_t id, bool group, char out[CODEPLUG_NAME_LEN + 1])
{
	(void)contact_text_lookup(id, group, out, CODEPLUG_NAME_LEN + 1);
}
#endif
#endif
