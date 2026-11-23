// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Shared display-name policy. Call from a task allowed to read the codeplug flash, never a drawing callback.
#ifndef RFOUNDRY_CONTACT_NAME_H
#define RFOUNDRY_CONTACT_NAME_H

#include <string.h>
#include "codeplug.h"
#include "dmr_iddb.h"

#if CONFIG_DMR
static inline void contact_name_lookup(uint32_t id, bool group, char out[CODEPLUG_NAME_LEN + 1])
{
	contact_t c;
	out[0] = '\0';
	if (id == 0 || id > 0xFFFFFFu)
		return;
	if (codeplug_contact_find(id, group, &c)) {
		memcpy(out, c.name, CODEPLUG_NAME_LEN + 1);
	} else if (!group) {
		char text[DMR_IDDB_TEXT_MAX + 1];
		if (dmr_iddb_lookup(id, text, sizeof(text))) {
			strncpy(out, text, CODEPLUG_NAME_LEN);
			out[CODEPLUG_NAME_LEN] = '\0';
		}
	}
}
#endif
#endif
