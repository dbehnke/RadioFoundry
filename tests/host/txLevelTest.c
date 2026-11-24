// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "dsp/mic_gain.h"
#include "dsp/tx_level.h"

#define N	160
#define TARGET	1843

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void tone(int16_t *pcm, int amplitude)
{
	for (unsigned i = 0; i < N; i++) pcm[i] = (int16_t)lrint(amplitude * cos(2 * M_PI * i / 40));
}

static int16_t db10(const int16_t *pcm)
{
	return tx_level_db10(tx_level_measure(pcm, N, true).power);
}

static void test_target(void)
{
	int16_t in[N], out[N];
	tx_level_t s;
	for (int amplitude = 200; amplitude <= 16000; amplitude *= 2) {
		tone(in, amplitude);
		tx_level_reset(&s, TX_LEVEL_UNITY);
		for (unsigned f = 0; f < 50; f++) tx_level_process(&s, in, out, N, 0, TARGET, NULL);
		CHECK(abs(db10(out) + 250) <= 2);
		CHECK(s.limited_blocks == 0);
	}

	// A loud onset after a quiet block sequence takes the RMS cut immediately.
	tone(in, 200);
	for (unsigned f = 0; f < 100; f++) tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	uint32_t held = s.gain_q16;
	CHECK(held > 10 * TX_LEVEL_UNITY && abs(db10(out) + 250) <= 2);
	memset(in, 0, sizeof in);
	tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	CHECK(s.gain_q16 == held && db10(out) == -960);
	tone(in, 50);
	tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	CHECK(s.gain_q16 == held);
	tone(in, 30000);
	CHECK(tx_level_process(&s, in, out, N, 0, TARGET, NULL) < TX_LEVEL_CEILING);
	CHECK(abs(db10(out) + 250) <= 2 && s.limited_blocks == 0);

	// The gain bounds apply when a steady block cannot reach the target within +/-24 dB.
	for (unsigned i = 0; i < N; i++) in[i] = TX_LEVEL_RMS_FLOOR - 1;
	uint32_t previous = s.gain_q16;
	tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	CHECK(s.gain_q16 == previous);
	for (unsigned i = 0; i < N; i++) in[i] = TX_LEVEL_RMS_FLOOR;
	tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	CHECK(s.gain_q16 > previous);
	for (unsigned f = 0; f < 100; f++) tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	CHECK(s.gain_q16 == TX_LEVEL_MAX_GAIN);
	for (unsigned i = 0; i < N; i++) in[i] = INT16_MIN;
	tx_level_process(&s, in, out, N, 0, TARGET, NULL);
	CHECK(s.gain_q16 == TX_LEVEL_MIN_GAIN && s.limited_blocks == 0);
}

static void test_manual(void)
{
	int16_t in[N], out[N];
	tx_level_t s;
	static const uint16_t gains_q8[] = { 81, 102, 128, 161, 203, 256, 322, 406, 511, 643, 810 };
	for (unsigned q = 0; q < sizeof gains_q8 / sizeof gains_q8[0]; q++) {
		uint32_t gain = (uint32_t)gains_q8[q] << 8;
		tone(in, 5000);
		tx_level_reset(&s, gain);
		tx_level_process(&s, in, out, N, gain, 0, NULL);
		for (unsigned i = 0; i < N; i++) CHECK(out[i] == (int32_t)in[i] * gains_q8[q] / 256);
		CHECK(s.gain_q16 == gain && s.limited_blocks == 0);
	}

	// A shout would clip with the manual boost. A block-wide cut keeps its shape.
	uint32_t gain = 810u << 8;
	tx_level_reset(&s, gain);
	tone(in, 18000);
	tx_level_meter_t raw;
	uint16_t peak = tx_level_process(&s, in, out, N, gain, 0, &raw);
	CHECK(raw.peak == 18000 && abs(tx_level_db10(raw.power) - db10(in)) <= 1);
	CHECK(peak > TX_LEVEL_CEILING - 2 && peak <= TX_LEVEL_CEILING && s.limited_blocks == 1);
	CHECK(s.gain_q16 < gain && abs(tx_level_db10((uint32_t)peak * peak) - db10(out) - 30) <= 1);

	// Quiet blocks gradually recover to the manual gain. The first contains a monotone ramp.
	for (unsigned i = 0; i < N; i++) in[i] = 1000;
	uint32_t before = s.gain_q16;
	tx_level_process(&s, in, out, N, gain, 0, NULL);
	CHECK(s.gain_q16 > before && s.gain_q16 <= before * TX_LEVEL_RELEASE_Q8 / 256);
	CHECK(out[0] - (int32_t)(1000 * before / TX_LEVEL_UNITY) <= 1);
	CHECK(out[N - 1] == (int32_t)(1000 * s.gain_q16 / TX_LEVEL_UNITY) && out[N - 1] > out[0]);
	for (unsigned i = 1; i < N; i++) CHECK(out[i] >= out[i - 1] && out[i] <= out[i - 1] + 1);
	unsigned blocks = 1;
	while (s.gain_q16 < gain && blocks < 30) {
		before = s.gain_q16;
		tx_level_process(&s, in, out, N, gain, 0, NULL);
		CHECK(s.gain_q16 > before && s.gain_q16 <= before * TX_LEVEL_RELEASE_Q8 / 256);
		blocks++;
	}
	CHECK(s.gain_q16 == gain && blocks >= 12 && blocks <= 14 && s.limited_blocks == 1);
	CHECK(out[N - 1] == 1000 * 810 / 256);

	// A manual reduction takes effect for the entire next block.
	tx_level_process(&s, in, out, N, TX_LEVEL_UNITY, 0, NULL);
	CHECK(s.gain_q16 == TX_LEVEL_UNITY && memcmp(in, out, sizeof in) == 0);
}

static void test_board_calibration(void)
{
	for (int db = MIC_GAIN_DB_MIN; db <= MIC_GAIN_DB_MAX; db++) {
		uint16_t q8 = mic_gain_db_q8(db);
		CHECK(fabs(q8 - 256.0 * pow(10.0, db / 20.0)) <= 0.5);
		CHECK(mic_gain_q8_db(q8) == db);
	}
	CHECK(mic_gain_db_q8(-40) == mic_gain_db_q8(MIC_GAIN_DB_MIN));
	CHECK(mic_gain_db_q8(99) == mic_gain_db_q8(MIC_GAIN_DB_MAX));
	CHECK(mic_gain_q8_db(0) == MIC_GAIN_DB_MIN && mic_gain_q8_db(UINT16_MAX) == MIC_GAIN_DB_MAX);

	int16_t in[N], out[N];
	uint32_t gain = (uint32_t)mic_gain_db_q8(12) << 8;
	tx_level_t s;
	tx_level_reset(&s, gain);
	// A -31 dBFS reference gets 12 dB of lift with no limiting.
	tone(in, 1306);
	uint16_t peak = tx_level_process(&s, in, out, N, gain, 0, NULL);
	CHECK(peak == 1306 * 1019 / 256 && abs(db10(out) + 190) <= 1 && s.limited_blocks == 0);
	// A loud sine stays a sine after the block-wide cut.
	tone(in, INT16_MAX);
	peak = tx_level_process(&s, in, out, N, gain, 0, NULL);
	CHECK(peak == TX_LEVEL_CEILING - 1 && s.limited_blocks == 1);
	CHECK(abs(tx_level_db10((uint32_t)peak * peak) - db10(out) - 30) <= 1);
	// Quiet blocks recover from that cut to the calibrated gain without exceeding it.
	tone(in, 1306);
	unsigned blocks = 0;
	while (s.gain_q16 < gain && blocks < 30) {
		uint32_t prev = s.gain_q16;
		tx_level_process(&s, in, out, N, gain, 0, NULL);
		CHECK(s.gain_q16 > prev && s.gain_q16 <= prev * TX_LEVEL_RELEASE_Q8 / 256);
		blocks++;
	}
	CHECK(s.gain_q16 == gain && blocks >= 25 && blocks <= 29 && s.limited_blocks == 1);
}

static void test_ceiling(void)
{
	int16_t pcm[1023] = { 0 };
	tx_level_t s;
	tx_level_meter_t raw;

	// The safe in-place path protects both PCM extremes, including unity.
	for (unsigned i = 0; i < 1023; i++) pcm[i] = INT16_MIN;
	tx_level_reset(&s, TX_LEVEL_UNITY);
	CHECK(tx_level_process(&s, pcm, pcm, 1023, TX_LEVEL_UNITY, 0, &raw) == TX_LEVEL_CEILING);
	CHECK(raw.peak == 32768 && raw.power == 1073741824u && pcm[0] == -TX_LEVEL_CEILING);
	CHECK(s.limited_blocks == 1);
	for (unsigned i = 0; i < 1023; i++) pcm[i] = INT16_MAX;
	CHECK(tx_level_process(&s, pcm, pcm, 1023, TX_LEVEL_UNITY, 0, &raw) == TX_LEVEL_CEILING - 1);
	CHECK(raw.peak == 32767 && raw.power == (uint32_t)INT16_MAX * INT16_MAX);
	CHECK(pcm[0] == TX_LEVEL_CEILING - 1 && s.limited_blocks == 2);

	// A sparse active peak at the P25 RMS target is ordinary attenuation. Below the activity floor, a
	// sparse peak must still cut inherited boost if it would cross the ceiling.
	tx_level_reset(&s, TX_LEVEL_UNITY);
	memset(pcm, 0, sizeof pcm);
	pcm[0] = INT16_MAX;
	tx_level_process(&s, pcm, pcm, N, 0, TARGET, &raw);
	CHECK(raw.peak == 32767 && s.limited_blocks == 0);
	tx_level_reset(&s, TX_LEVEL_MAX_GAIN);
	pcm[0] = 2000;
	CHECK(tx_level_process(&s, pcm, pcm, 1023, 0, TARGET, &raw) == TX_LEVEL_CEILING);
	CHECK(raw.power < TX_LEVEL_RMS_FLOOR * TX_LEVEL_RMS_FLOOR && s.limited_blocks == 1);

	tx_level_reset(&s, TX_LEVEL_UNITY);
	CHECK(s.gain_q16 == TX_LEVEL_UNITY && s.limited_blocks == 0);
	tone(pcm, 10000);
	int16_t copy[N];
	memcpy(copy, pcm, sizeof copy);
	tx_level_process(&s, pcm, pcm, N, TX_LEVEL_UNITY, 0, NULL);
	CHECK(memcmp(copy, pcm, sizeof copy) == 0 && s.limited_blocks == 0);
}

static void test_meter(void)
{
	int16_t quiet[N];
	for (unsigned i = 0; i < N; i++) quiet[i] = i & 1u ? 15 : -15;
	tx_level_meter_t raw = tx_level_measure(quiet, N, true);
	CHECK(raw.power == 225 && raw.peak == 15);
	CHECK(abs(tx_level_db10(raw.power) + 668) <= 1);
	CHECK(tx_level_db10(0) == -960);
	CHECK(tx_level_db10(1073741824u) == 0);
	for (int db = -45; db <= -5; db++) {
		uint32_t p = (uint32_t)(1073741824.0 * pow(10, db / 10.0));
		CHECK(abs(tx_level_db10(p) - 10 * db) <= 1);
	}
}

int main(void)
{
	test_target();
	test_manual();
	test_board_calibration();
	test_ceiling();
	test_meter();
	printf("txLevelTest: %u checks, %u failed\n", checks, failures);
	return failures != 0;
}
