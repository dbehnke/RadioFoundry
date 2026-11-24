// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef RFOUNDRY_P25_PARROT_H
#define RFOUNDRY_P25_PARROT_H
#include <stdint.h>
#include <stdbool.h>
enum { P25_PARROT_OFF, P25_PARROT_READY, P25_PARROT_RECORD, P25_PARROT_PLAY, P25_PARROT_DONE, P25_PARROT_ERROR,
	P25_PARROT_OPENING, P25_PARROT_REFUSED, P25_PARROT_CLOSING };
enum { P25_PARROT_OPEN, P25_PARROT_CAPTURE, P25_PARROT_STOP, P25_PARROT_CLOSE };
typedef struct {
	uint8_t state;
	uint16_t frames;
	int16_t input_db10, encoder_db10, output_db10;
	uint16_t input_peak, encoder_peak, output_peak;
	uint32_t gain_q16, manual_gain_q16, limited_blocks;
	bool agc, limiter;
	int8_t target_dbfs;
	uint8_t mic_step;
} p25_parrot_view_t;
#endif
