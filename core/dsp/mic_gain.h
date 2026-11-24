// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef RFOUNDRY_DSP_MIC_GAIN_H
#define RFOUNDRY_DSP_MIC_GAIN_H
#include <stdint.h>

// Manual microphone steps 0..10: 2 dB per step, unity at 5. Keep the established Q8 rounding.
static inline uint16_t mic_gain_step_q8(uint8_t step)
{
	static const uint16_t q8[11] = { 81, 102, 128, 161, 203, 256, 322, 406, 511, 643, 810 };
	return q8[step > 10 ? 10 : step];
}
#endif
