// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef RFOUNDRY_P25_PARROT_H
#define RFOUNDRY_P25_PARROT_H
#include <stdint.h>
#include <stdbool.h>
enum { P25_PARROT_OFF, P25_PARROT_READY, P25_PARROT_RECORD, P25_PARROT_PLAY, P25_PARROT_DONE, P25_PARROT_ERROR,
	P25_PARROT_OPENING, P25_PARROT_REFUSED, P25_PARROT_CLOSING };
enum { P25_PARROT_OPEN, P25_PARROT_CAPTURE, P25_PARROT_STOP, P25_PARROT_CLOSE };
enum { CODEC_DIAG_P25, CODEC_DIAG_CALIBRATE };
#define P25_CAL_PASSES 6u
#define P25_CAL_MAX_BLOCKS 100u
// A recommendation is available only after an independent final encode/decode pass verifies it.
enum { P25_CAL_IDLE, P25_CAL_RUNNING, P25_CAL_OK, P25_CAL_SHORT, P25_CAL_CLIPPED,
	P25_CAL_LIMITED, P25_CAL_RANGE, P25_CAL_UNAVAILABLE };
typedef struct {
	int16_t target_db10, encoder_db10, decoded_db10;
	uint16_t active_blocks, limited_blocks;
	uint32_t gain_q16;
} p25_cal_result_t;
typedef struct {
	uint8_t state, mode;
	bool dc_block;
	int16_t conditioned_db10;
	uint32_t codec_options, codec_flags; // reserved compatibility fields, zero in the P25-only diagnostic
	uint8_t cal_status, cal_pass;
	int16_t suggested_db10;
	uint16_t capture_limit;
	uint32_t startup_rail_samples;
	p25_cal_result_t cal[P25_CAL_PASSES];
	uint16_t frames;
	int16_t input_db10, encoder_db10, output_db10;
	uint16_t input_peak, encoder_peak, output_peak;
	uint32_t gain_q16, manual_gain_q16, limited_blocks;
	bool agc, limiter;
	int8_t target_dbfs;
	uint8_t mic_step;
	int16_t mic_mean, mic_ac_db10;
	uint16_t mic_dc_peak;
	uint32_t input_rail_samples;
	// Mic, encoder and decoded PCM: mean-removed, gated energy, using the receive meter's rules.
	int16_t speech_db10[3];
	uint16_t speech_blocks[3];
} p25_parrot_view_t;
#endif
