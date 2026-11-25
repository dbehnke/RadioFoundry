// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Display-only VHF/UHF signal scale. Receiver decisions continue to use their unmodified measurements.
#ifndef UI_SIGNAL_H
#define UI_SIGNAL_H
#include <stdint.h>
#include <stdbool.h>

static inline uint8_t ui_signal_segments(int dbm)
{
	static const int16_t threshold[13] = { -141, -135, -129, -123, -117, -111, -105, -99, -93, -83, -73, -53, -33 };
	uint8_t n = 0;
	while (n < 13 && dbm >= threshold[n])
		n++;
	return n;
}

static inline int ui_signal_correct(int16_t dbm, int correction)
{
	return (int)dbm + (correction >= -20 && correction <= 20 ? correction : 0);
}
#endif
