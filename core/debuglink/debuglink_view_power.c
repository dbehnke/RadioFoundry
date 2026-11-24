// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Runtime power metadata has its own reply so band labels cannot truncate the core view.
#include "board.h"
#include "debuglink/debuglink.h"
#include "settings/settings.h"

void debuglink_view_power(dl_text_t *t)
{
	dl_text(t, "power_count=%u\n", board.power_count);
	uint8_t factory = board.power_default < board.power_count ? board.power_default :
			  board.power_count ? (uint8_t)(board.power_count - 1u) : 0;
	uint8_t master = SETTINGS_FIELD(master_power);
	dl_text(t, "power_factory=%u\npower_default=%u\n", factory, master < board.power_count ? master : factory);
	// Runtime labels describe this image's variant, including bands the current channel is not using.
	dl_text(t, "power_base_names=");
	for (uint8_t l = 0; l < board.power_count; l++)
		dl_text(t, l ? "/%s" : "%s", board.power_names != NULL ? board.power_names[l] : "?");
	dl_text(t, "\npower_bands=");
	for (uint8_t b = 0; board.power_bands != NULL && b < board.power_band_count; b++) {
		const board_power_band_t *band = &board.power_bands[b];
		dl_text(t, "%s%u-%u:", b ? "," : "", (unsigned)band->lo_hz, (unsigned)band->hi_hz);
		for (uint8_t l = 0; l < board.power_count; l++)
			dl_text(t, l ? "/%s" : "%s", band->names != NULL ? band->names[l] : "?");
	}
	dl_text(t, "\n");
}
