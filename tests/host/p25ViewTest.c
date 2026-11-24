// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// The P25 GET_VIEW reply budget during conventional receive and after a codec diagnostic.
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "debuglink/debuglink.h"
#include "debuglink/dl_frame.h"
#include "p25/p25Voice.h"
#include "radio/radio_p25.h"
#include "settings/settings.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static bool load_available;
static bool running = true;
static p25_parrot_view_t parrot;

static bool rx_load(uint32_t *work, uint32_t *span)
{
	*work = 1406478;
	*span = 3786100;
	return load_available;
}

static const hal_air_t air = { .rx_load = rx_load };
const board_t board = { .air = &air };

uint32_t settings_read_field(size_t offset, size_t size)
{
	return offset == offsetof(settings_t, caller_flags) ? SETTINGS_CALLER_P25_DMR : 0;
}

void p25VoiceParrotView(p25_parrot_view_t *out) { *out = parrot; }

void radio_p25_settings(radio_p25_settings_t *out)
{
	*out = (radio_p25_settings_t){
		.rx_nac = 659, .tx_nac = 659, .rx_tgid = 1, .talkaround = RADIO_P25_TA_ON,
		.tx_dst = 293, .unit_id = 2718281,
	};
}

bool radio_p25_in_force(radio_p25_settings_t *out)
{
	radio_p25_settings(out);
	return running;
}

void radio_p25_stats(radio_p25_stats_t *out)
{
	*out = (radio_p25_stats_t){
		.begins = 1, .rx_calls = 1, .units = 1, .voice = 98, .rx_chunks = 35430,
	};
}

void p25VoiceGetStats(p25_voice_stats_t *out)
{
	*out = (p25_voice_stats_t){ .rx_in = 98, .rx_frames = 98, .rx_calls = 1, .rx_queue_max = 9, .rx_peak = 15367 };
}

void radio_p25_reset_stats(void) {}
void p25VoiceResetStats(void) {}

static void render(dl_text_t *t, char *text, size_t cap)
{
	static radio_view_t view;
	view.p25 = (radio_p25_view_t){
		.flags = running ? RADIO_P25_ON | RADIO_P25_RX | RADIO_P25_PLAYED : 0,
		.nac = 659, .src = 3141592, .dst = 293,
	};
	dl_text_init(t, text, cap);
	dl_text(t, "section=p25\n");
	debuglink_view_p25(&view, t);
	text[t->len] = '\0';
}

static void check_conventional(const char *text)
{
	CHECK(strstr(text, "p25.lookup.built=1\np25.lookup.enabled=1\n") != NULL);
	CHECK(strstr(text, "p25parrot.built=1\np25parrot.state=0\n") != NULL);
	CHECK(strstr(text, "p25parrot.frames=") == NULL);
	CHECK(strstr(text, "p25.tx_filler=0\n") != NULL);
	CHECK(strstr(text, "p25.units=1\n") != NULL);
	CHECK(strstr(text, "p25.voice=98\n") != NULL);
	CHECK(strstr(text, "p25.voice_bad=0\n") != NULL);
	CHECK(strstr(text, "p25.voice_bits=0\n") != NULL);
}

int main(void)
{
	// cmd_get_view reserves 6 bytes for its header and 16 for a possible truncation marker.
	char text[DL_REPLY_MAX - 6u - 16u];
	dl_text_t t;
	render(&t, text, sizeof(text));
	CHECK(!t.full);
	check_conventional(text);
	CHECK(strstr(text, "p25.rx_work_us=") == NULL);
	CHECK(strstr(text, "p25v.tx_encoded=0\np25v.tx_dropped=0\np25v.tx_silent=0\np25v.tx_mic_peak=0\n") != NULL);
	size_t without_load = t.len;

	load_available = true;
	render(&t, text, sizeof(text));
	check_conventional(text);
	CHECK(strstr(text, "p25.rx_work_us=1406478\np25.rx_span_us=3786100\n") != NULL);
	size_t with_load = t.len;

	running = false;
	parrot = (p25_parrot_view_t){
		.state = P25_PARROT_DONE, .frames = 400,
		.input_db10 = -541, .encoder_db10 = -351, .output_db10 = -410,
		.input_peak = 4189, .encoder_peak = 28000, .output_peak = 21393,
		.gain_q16 = 181963, .manual_gain_q16 = 164619, .limited_blocks = 104,
		.agc = true, .limiter = true, .target_dbfs = -25, .mic_step = 9,
	};
	render(&t, text, sizeof(text));
	static const char *const results[] = {
		"p25parrot.built=1\n", "p25parrot.state=4\n", "p25parrot.frames=400\n",
		"p25parrot.input_db10=-541\n", "p25parrot.encoder_db10=-351\n", "p25parrot.output_db10=-410\n",
		"p25parrot.input_peak=4189\n", "p25parrot.encoder_peak=28000\n", "p25parrot.output_peak=21393\n",
		"p25parrot.gain_q16=181963\n", "p25parrot.agc=1\n", "p25parrot.target_dbfs=-25\n",
		"p25parrot.limited_blocks=104\n", "p25parrot.mode=P25/IMBE\n", "p25parrot.mic_step=9\n",
		"p25parrot.manual_gain_q16=164619\n", "p25parrot.limiter=1\n",
	};
	for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
		if (strstr(text, results[i]) == NULL) {
			printf("FAIL missing %s", results[i]);
			failures++;
		}
	}
	CHECK(strstr(text, "p25.on=0\n") != NULL);
	printf("p25ViewTest: RX %zu bytes, RX with load %zu bytes, diagnostic %zu bytes, %d failures\n",
	       without_load, with_load, t.len, failures);
	return failures != 0;
}
