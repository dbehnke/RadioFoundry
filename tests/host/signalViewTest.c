// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Display-only diagnostics retain explicit validity and fit independently of core counters.
#include <stdio.h>
#include <string.h>
#include "debuglink/debuglink.h"
#include "debuglink/dl_frame.h"
#include "settings/settings.h"
static unsigned checks, failures;
static uint8_t flags;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
uint32_t settings_read_field(size_t offset, size_t size)
{
	CHECK(offset == offsetof(settings_t, sound_flags));
	CHECK(size == 1);
	return flags;
}
int main(void)
{
	radio_view_t v = {0};
	char buf[DL_REPLY_MAX] = {0};
	dl_text_t t;
#if CONFIG_RX_SIGNAL_MENU
	v.signal = (radio_signal_t){.dbm=-140, .state=RADIO_SIGNAL_CURRENT, .source=RADIO_SIGNAL_SOURCE_RF,
		.sampled_ms=4294967295u, .rx_hz=4294967295u};
	flags = SETTINGS_RX_SIGNAL_DETAILS | SETTINGS_KNOB_LOCK;
#endif
	dl_text_init(&t, buf, sizeof(buf)-6u-16u);
	debuglink_view_signal(&v, &t);
	CHECK(!t.full && t.len < 1000u);
	buf[t.len] = 0;
#if CONFIG_RX_SIGNAL_MENU
	CHECK(strstr(buf, "signal.built=1\nsignal.details=1\n") != NULL);
	CHECK(strstr(buf, "signal.dbm=-140\nsignal.state=1\nsignal.source=1\n") != NULL);
	CHECK(strstr(buf, "signal.sampled_ms=4294967295\nsignal.rx_hz=4294967295\n") != NULL);
	flags = SETTINGS_TONES_OFF_MASK;
	dl_text_init(&t, buf, sizeof(buf)-6u-16u);
	debuglink_view_signal(&v, &t);
	buf[t.len] = 0;
	CHECK(strstr(buf, "signal.details=0\n") != NULL);
#else
	CHECK(strcmp(buf, "signal.built=0\n") == 0);
#endif
	printf("signalViewTest: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
