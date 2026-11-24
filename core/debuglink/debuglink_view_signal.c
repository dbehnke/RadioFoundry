// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Keep presentation metadata out of the size-limited core diagnostic reply.
#include "config.h"
#include "debuglink/debuglink.h"
#include "settings/settings.h"

void debuglink_view_signal(const radio_view_t *v, dl_text_t *t)
{
#if CONFIG_RX_SIGNAL_MENU
	dl_text(t, "signal.built=1\nsignal.details=%u\n", !!(SETTINGS_FIELD(sound_flags) & SETTINGS_RX_SIGNAL_DETAILS));
	dl_text(t, "signal.dbm=%d\nsignal.state=%u\nsignal.source=%u\nsignal.sampled_ms=%u\nsignal.rx_hz=%u\n",
		v->signal.dbm, v->signal.state, v->signal.source, (unsigned)v->signal.sampled_ms, (unsigned)v->signal.rx_hz);
#else
	(void)v;
	dl_text(t, "signal.built=0\n");
#endif
}
