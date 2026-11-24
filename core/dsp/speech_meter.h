// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Shared read-only speech energy estimate for receive and local codec diagnostics.
#ifndef RFOUNDRY_SPEECH_METER_H
#define RFOUNDRY_SPEECH_METER_H
#include <stdint.h>
#define SPEECH_ACQUIRE_BLOCKS 15u
#define SPEECH_AVERAGE_BLOCKS 75u
#define SPEECH_FLOOR_Q8 (-55 * 256)

// One PCM block, 1..160 samples. A block mean is removed only for measurement.
static inline uint32_t speech_power(const int16_t *pcm, unsigned n, uint32_t *raw)
{
	int32_t mean = 0;
	uint64_t raw_energy = 0;
	for (unsigned i = 0; i < n; i++) {
		mean += pcm[i];
		if (raw)
			raw_energy += (uint32_t)((int32_t)pcm[i] * pcm[i]);
	}
	if (raw)
		*raw = (uint32_t)((raw_energy + n - 1u) / n);
	mean /= (int32_t)n;
	uint64_t energy = 0;
	for (unsigned i = 0; i < n; i++) {
		int32_t v = (int32_t)pcm[i] - mean;
		energy += (uint64_t)((int64_t)v * v);
	}
	return (uint32_t)(energy / n);
}

// 10 log10(mean square / 32768^2), Q8. Binary logarithm avoids floating point in the audio callback.
static inline int16_t speech_level_q8(uint32_t power)
{
	if (power == 0)
		return -96 * 256;
	unsigned bit = 0;
	for (uint32_t v = power; v > 1; v >>= 1)
		bit++;
	uint32_t v = bit > 16 ? power >> (bit - 16) : power << (16 - bit);
	int32_t log = (int32_t)bit * 256;
	for (unsigned f = 128; f != 0; f >>= 1) {
		v = (uint32_t)(((uint64_t)v * v) >> 16);
		if (v >= 131072u) {
			v >>= 1;
			log += (int32_t)f;
		}
	}
	return (int16_t)((log - 30 * 256) * 771 / 256);
}

// Average energy before converting to dB, so quiet syllables do not bias the talker estimate downward.
static inline uint32_t speech_average(uint32_t previous, uint32_t current, unsigned blocks)
{
	unsigned divisor = blocks < SPEECH_ACQUIRE_BLOCKS ? blocks + 1 : SPEECH_AVERAGE_BLOCKS;
	return (uint32_t)((int32_t)previous + ((int32_t)current - (int32_t)previous) / (int32_t)divisor);
}

#endif
