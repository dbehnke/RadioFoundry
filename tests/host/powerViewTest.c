// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Runtime power labels/defaults, including per-band names, fit a GET_VIEW reply independently of core counters.
#include <stdio.h>
#include <string.h>
#define board board_as_declared
#include "board.h"
#undef board
#include "debuglink/debuglink.h"
#include "debuglink/dl_frame.h"
#include "settings/settings.h"

board_t board;
static uint8_t master;
static unsigned failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

uint32_t settings_read_field(size_t offset, size_t size)
{
	CHECK(offset == offsetof(settings_t, master_power));
	CHECK(size == sizeof(((settings_t *)0)->master_power));
	return master;
}

static char text[DL_REPLY_MAX];
static size_t render(void)
{
	dl_text_t t;
	dl_text_init(&t, text, DL_REPLY_MAX - 6u - 16u);
	dl_text(&t, "section=power\n");
	debuglink_view_power(&t);
	CHECK(!t.full);
	CHECK(t.len <= 1000u);
	text[t.len] = '\0';
	return t.len;
}

int main(void)
{
	static const char *const names[] = { "100mW", "1W", "5W", "25W", "45W" };
	static const char *const vhf[] = { "250mW", "1W", "5W", "25W", "50W" };
	static const board_power_band_t bands[] = {
		{136000000u, 174000000u, vhf}, {400000000u, 480000000u, names}
	};
	board = (board_t){.power_count=5, .power_default=2, .power_names=names,
		.power_bands=bands, .power_band_count=2};
	master = SETTINGS_UNSET8;
	render();
	CHECK(strstr(text, "power_count=5\npower_factory=2\npower_default=2\n") != NULL);
	CHECK(strstr(text, "power_base_names=100mW/1W/5W/25W/45W\n") != NULL);
	CHECK(strstr(text, "power_bands=136000000-174000000:250mW/1W/5W/25W/50W,400000000-480000000:100mW/1W/5W/25W/45W\n") != NULL);
	master = 4;
	render();
	CHECK(strstr(text, "power_default=4\n") != NULL);
	master = 5;
	render();
	CHECK(strstr(text, "power_default=2\n") != NULL);
	board.power_default = 255;
	render();
	CHECK(strstr(text, "power_factory=4\npower_default=4\n") != NULL);

	// A larger label table than the current boards, on two bands with widest frequency fields.
	static const char *const wide[] = {"100mW", "250mW", "500mW", "750mW", "1W", "2W", "3W", "4W", "5W", "10W"};
	static const board_power_band_t wide_bands[] = {
		{1000000000u, 2000000000u, wide}, {3000000000u, 4000000000u, wide}
	};
	board.power_count = 10;
	board.power_names = wide;
	board.power_bands = wide_bands;
	size_t wide_len = render();
	CHECK(strstr(text, "power_count=10\n") != NULL);
	CHECK(strstr(text, "power_factory=9\npower_default=5\n") != NULL);

	static const board_power_band_t unnamed[] = {{136000000u, 174000000u, NULL}};
	board.power_bands = unnamed;
	board.power_band_count = 1;
	render();
	CHECK(strstr(text, "power_bands=136000000-174000000:?/?/?/?/?/?/?/?/?/?\n") != NULL);

	board.power_names = NULL;
	board.power_bands = NULL;
	render();
	CHECK(strstr(text, "power_base_names=?/?/?/?/?/?/?/?/?/?\npower_bands=\n") != NULL);
	board.power_count = 0;
	render();
	CHECK(strstr(text, "power_count=0\npower_factory=0\npower_default=0\npower_base_names=\npower_bands=\n") != NULL);
	printf("powerViewTest: %u checks, %u failures; wide reply %zu text bytes\n", checks, failures, wide_len);
	return failures != 0;
}
