// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#ifndef RFOUNDRY_DSP_MIC_GAIN_H
#define RFOUNDRY_DSP_MIC_GAIN_H
#include <stdint.h>

#define MIC_GAIN_DB_MIN	(-12)
#define MIC_GAIN_DB_MAX	24

// Manual microphone steps 0..10: 2 dB per step, unity at 5. Keep the established Q8 rounding.
static inline uint16_t mic_gain_step_q8(uint8_t step)
{
	static const uint16_t q8[11] = { 81, 102, 128, 161, 203, 256, 322, 406, 511, 643, 810 };
	return q8[step > 10 ? 10 : step];
}

// Board calibration plus the manual step, 256 x 10^(dB/20), rounded and clamped to the table's range.
static inline uint16_t mic_gain_db_q8(int db)
{
	static const uint16_t q8[MIC_GAIN_DB_MAX - MIC_GAIN_DB_MIN + 1] = {
		64, 72, 81, 91, 102, 114, 128, 144, 162, 181, 203, 228,			// -12..-1
		256, 287, 322, 362, 406, 455, 511, 573, 643, 722, 810, 908,		// 0..11
		1019, 1144, 1283, 1440, 1615, 1812, 2033, 2282, 2560, 2872, 3223, 3616,	// 12..23
		4057,									// 24
	};
	if (db < MIC_GAIN_DB_MIN) {
		db = MIC_GAIN_DB_MIN;
	} else if (db > MIC_GAIN_DB_MAX) {
		db = MIC_GAIN_DB_MAX;
	}
	return q8[db - MIC_GAIN_DB_MIN];
}

// Nearest whole dB within the table's range, for reporting the limiter's lowest gain.
static inline int mic_gain_q8_db(uint16_t q8)
{
	int db = MIC_GAIN_DB_MIN;
	for (; db < MIC_GAIN_DB_MAX; db++) {
		uint16_t lo = mic_gain_db_q8(db), hi = mic_gain_db_q8(db + 1);
		// The midpoint in dB is the geometric mean of the adjacent linear gains.
		if ((uint32_t)q8 * q8 <= (uint32_t)lo * hi) {
			break;
		}
	}
	return db;
}
#endif
