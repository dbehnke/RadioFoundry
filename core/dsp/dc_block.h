// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Streaming microphone DC removal. The estimate belongs to one capture/route, not a radio calibration.
#ifndef RFOUNDRY_DC_BLOCK_H
#define RFOUNDRY_DC_BLOCK_H
#include <stdint.h>
#include <stdbool.h>
#include "dsp/fxp.h"

typedef struct { int32_t mean_q8; bool primed; } dc_block_t;

static inline void dc_block_reset(dc_block_t *s)
{
	s->mean_q8 = 0;
	s->primed = false;
}

// 1..160 samples at 8 kHz; input/output may alias. A 1/256 estimator gives a roughly 5 Hz high-pass.
// Prime from the first block's mean so a steady startup bias does not become a large encoder transient.
static inline void dc_block_process(dc_block_t *s, const int16_t *in, int16_t *out, unsigned n)
{
	if (!s->primed) {
		int32_t sum = 0;
		for (unsigned i = 0; i < n; i++) sum += in[i];
		int32_t sum_q8 = sum * 256, count = (int32_t)n;
		s->mean_q8 = sum_q8 / count - (sum_q8 % count < 0);
		s->primed = true;
	}
	for (unsigned i = 0; i < n; i++) {
		int32_t sample = in[i];
		// Floor division gives the same residual for either polarity of an integer-sample bias.
		int32_t mean = (s->mean_q8 + 8388608) / 256 - 32768;
		out[i] = (int16_t)fxp_sat16(sample - mean);
		s->mean_q8 += (sample * 256 - s->mean_q8) / 256;
	}
}
#endif
