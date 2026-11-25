// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Review-only compositions: real framebuffer, fonts and safe areas, synthetic content.
// This program is deliberately absent from firmware and the production UI gallery.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "sim/ui_sim.h"
#include "ui/ui_screens.h"
#include "ui/ui_svc.h"
#include "ui/gfx.h"

static color_t strip[320 * 40];
static color_t baseline[320 * 320];
static int strip_h;
static const char *source = "";
static const int thresholds[] = { -141, -135, -129, -123, -117, -111, -105, -99, -93, -83, -73, -53, -33 };
static int count(int dbm)
{
	int n = 0;
	while (n < 13 && dbm >= thresholds[n]) n++;
	return n;
}
static void label(int dbm, char *out, size_t cap)
{
	if (dbm < -141) snprintf(out, cap, "<S1");
	else if (dbm <= -93) snprintf(out, cap, "S%d", 1 + (dbm + 141) / 6);
	else snprintf(out, cap, "+%d", dbm + 93);
}
static void text(const font_t *f, int x, int y, const char *s, color_t c)
{
	assert(y >= 0 && y + f->h <= gfx_height());
	assert(x >= 0 && x + gfx_text_width(f, s) <= gfx_width());
	gfx_text(f, x, y, s, c);
}
static void fit(const font_t *f, int x, int y, int right, const char *s, color_t c)
{
	char buf[100];
	gfx_text_fit(f, s, right - x, buf, sizeof buf);
	text(f, x, y, buf, c);
}
static void meter(int y, int dbm, bool literal)
{
	const font_t *f = ui_layout.tiny;
	char unit[16], strength[12], value[16];
	label(dbm, strength, sizeof strength);
	snprintf(unit, sizeof unit, "%s%s", source, strength);
	snprintf(value, sizeof value, "%d", dbm);
	text(f, 0, y, unit, ui_col.fg);
	int right = gfx_width() - gfx_text_width(f, value);
	text(f, right, y, value, ui_col.fg);
	int left = 6 * f->w, width = right - f->w - left;
	int pitch = width / 13, cw = pitch - 1;
	assert(cw >= 3);
	int h = f->h >= 12 ? 9 : 7;
	int cy = y + (f->h - h) / 2;
	for (int i = 0; i < 13; i++) {
		color_t c = gfx_bpp() == 1 ? 1 : i < 9 ? ui_col.rx : i < 11 ? ui_col.warn : ui_col.tx;
		int x = left + i * pitch;
		if (i < count(dbm)) {
			if (literal && i < 9) gfx_rect(x, cy, cw, h, c);
			else gfx_fill(x, cy, cw, h, c);
		} else {
			if (!literal) gfx_rect(x, cy, cw, h, ui_col.line);
			else gfx_hline(x, cy + h - 1, cw, ui_col.line);
		}
	}
	gfx_vline(left + 9 * pitch - 1, cy - 1, h + 2, ui_col.fg);
}
static void top(const char *mode)
{
	gfx_clear(ui_col.bg);
	for (int y = 0; y < strip_h; y++)
		for (int x = 0; x < gfx_width(); x++) gfx_pixel(x, y, strip[y * gfx_width() + x]);
}
static void hint(const char *s)
{
	fit(ui_layout.tiny, 0, gfx_height() - ui_layout.tiny->h, gfx_width(), s, ui_col.dim);
}
static void pane(int y, int rows, const char *tag, const char *name, const char *body, const char *detail, bool active)
{
	const font_t *nf = ui_layout.medium, *sf = ui_layout.small, *df = ui_layout.tiny;
	int tw = nf->w + 4, x = tw + 3;
	int height = nf->h + (rows == 3 ? sf->h + df->h : df->h);
	gfx_rect(0, y, tw, height, active ? ui_col.rx : ui_col.line);
	text(nf, 2, y + (height - nf->h) / 2, tag, active ? ui_col.rx : ui_col.fg);
	const char *state = strcmp(tag, "A") == 0 ? "PTT" : active ? "RX" : "";
	int right = gfx_width();
	if (*state) {
		right -= gfx_text_width(df, state);
		text(df, right, y + (nf->h - df->h) / 2, state, active ? ui_col.rx : ui_col.fg);
		right -= df->w;
	}
	fit(nf, x, y, right, name, ui_col.fg);
	if (rows == 3) {
		fit(sf, x, y + nf->h, gfx_width(), body, active ? ui_col.rx : ui_col.fg);
		fit(df, x, y + nf->h + sf->h, gfx_width(), detail, ui_col.dim);
	} else fit(df, x, y + nf->h, gfx_width(), detail, ui_col.dim);
}
static void proposed(int scene, const char *power)
{
	bool tall = gfx_height() > 200, mono = gfx_bpp() == 1;
	const font_t *nf = ui_layout.medium, *sf = ui_layout.small, *df = ui_layout.tiny;
	char detail[64];
	if ((mono && scene <= 2) || scene == 8) {
		for (int y = 0; y < gfx_height(); y++)
			for (int x = 0; x < gfx_width(); x++) gfx_pixel(x, y, baseline[y * gfx_width() + x]);
		if (scene != 8) {
			gfx_fill(0, gfx_height() - df->h, gfx_width(), df->h, ui_col.bg);
			snprintf(detail, sizeof detail, "A %s  # options  Back", power);
			hint(detail);
		}
		return;
	}
	if (scene <= 2) {
		const char *mode = scene == 0 ? "FM DUAL" : scene == 1 ? "DMR DUAL" : "P25 DUAL";
		top(mode);
		int y = sf->h + (tall ? 12 : 3);
		snprintf(detail, sizeof detail, scene == 0 ? "T100.0 25k %s SQ5" : scene == 1 ? "DMR TS1 CC1 %s" : "P25 %s", power);
		pane(y, mono ? 2 : 3, "A", scene == 0 ? "2 Valley rpt" : "3 COUNTY DMR", scene == 0 ? "147.060 MHz" : scene == 1 ? "TG 3100 USA 453.625" : "453.625 MHz", detail, false);
		y += nf->h + (mono ? df->h : sf->h + df->h) + (tall ? 18 : 4);
		snprintf(detail, sizeof detail, scene == 0 ? "DMR TS1 CC1 %s" : "T100.0 25k %s SQ5", power);
		pane(y, mono ? 2 : 3, "B", scene == 0 ? "3 COUNTY DMR" : "4 County fire", scene == 0 ? "TG 3100 USA 453.625" : "154.250 MHz", detail, false);
		hint("# options  Back stop");
	} else if (scene == 3 || scene >= 9) {
		top("DMR DUAL");
		int y = sf->h + (tall ? 12 : 2);
		if (mono) {
			fit(sf, 0, y, gfx_width(), scene == 9 ? "A 147.060 RX PTT" : scene == 10 ? "A ID 9991234 PTT" : "A Q1ABC Joe PTT", ui_col.fg);
			y += sf->h + 2;
			fit(sf, 0, y, gfx_width(), scene == 9 ? "B COUNTY DMR" : scene == 11 ? "B Q4WAB Eve RX" : "B County fire", ui_col.rx);
			y += sf->h;
			snprintf(detail, sizeof detail, scene == 10 ? "P25 %s" : scene == 9 ? "T100.0 25k %s SQ5" : "DMR TS1 %s", power);
			fit(df, 0, y, gfx_width(), detail, ui_col.dim);
		} else {
			snprintf(detail, sizeof detail, scene == 9 ? "T100.0 25k %s SQ5" : scene == 10 ? "P25 %s" : "DMR TS1 CC1 %s", power);
			pane(y, 3, "A", scene == 9 ? "2 Valley rpt" : "3 COUNTY DMR", scene == 9 ? "147.060 MHz" : scene == 10 ? "ID 9991234" : "Q1ABC Joe", detail, true);
			y += nf->h + sf->h + df->h + (tall ? 18 : 5);
			snprintf(detail, sizeof detail, scene == 9 || scene == 11 ? "DMR TS1 CC1 %s" : "T100.0 25k %s SQ5", power);
			pane(y, 3, "B", scene == 9 ? "3 COUNTY DMR" : scene == 11 ? "5 Metro DMR" : "4 County fire", scene == 11 ? "Q4WAB Eve" : scene == 9 ? "TG 3100 USA 453.625" : "154.250 MHz", detail, scene == 11);
			if (tall) {
				y += nf->h + sf->h + df->h + 8;
				text(sf, 0, y, scene == 9 ? "" : scene == 10 ? "NAC 293 TG 7" : "ID 9991234  TG 91", ui_col.dim);
				
			}
		}
		if (!tall && scene != 9 && scene != 10)
			fit(df, 0, gfx_height() - 2 * df->h - 2, gfx_width(), scene == 11 ? "A 9991234 B 9997777" : "A ID 9991234 TG 91", ui_col.dim);
		source = scene == 11 ? "A* " : "A ";
		meter(gfx_height() - df->h, -73, false);
		source = "";
	} else if (scene == 8) {
		top("DUAL");
		int y = sf->h + (tall ? 22 : 5), pitch = (gfx_height() - y - df->h) / 2;
		fit(ui_layout.large, 0, y, gfx_width(), "A COUNTY DMR", ui_col.fg);
		fit(ui_layout.large, 0, y + pitch, gfx_width(), scene == 9 ? "B COUNTY DMR" : scene == 11 ? "B Q4WAB Eve RX" : "B County fire", ui_col.rx);
		hint("Large text: unchanged");
	} else {
		top(scene == 6 ? "P25 RX" : scene == 7 ? "FM RX" : "DMR RX");
		int y = sf->h + (tall ? 14 : 2);
		nf = ui_layout.large;
		if (tall) { fit(ui_layout.medium, 0, y, gfx_width(), scene == 7 ? "2 Valley rpt" : "3 COUNTY DMR", ui_col.fg); y += ui_layout.medium->h + 8; }
		const char *caller = scene == 6 ? "ID 9995678" : scene == 7 ? "147.060 MHz" : scene == 5 ? "Q1ABC Elizabeth" : "Q1ABC Joe";
		if (gfx_text_width(nf, caller) > gfx_width()) nf = ui_layout.medium;
		fit(nf, 0, y, gfx_width(), caller, ui_col.rx);
		y += nf->h + (tall ? 8 : 1);
		if (tall && scene != 6 && scene != 7) {
			fit(sf, 0, y, gfx_width(), scene == 5 ? "Elizabeth Montgomery" : "Joe Smith", ui_col.fg);
			y += sf->h + 8;
			text(sf, 0, y, "ID 9991234", ui_col.dim);
			y += sf->h + 8;
		}
		if (tall && scene == 6) {
			text(sf, 0, y, "NAC 293 TG 7", ui_col.dim);
			y += sf->h + 8;
		}
		if (!tall && scene != 7) {
			fit(df, 0, y, gfx_width(), scene == 6 ? "NAC 293 TG 7" : "ID 9991234 TG 91", ui_col.dim);
			y += df->h + 1;
		}
		snprintf(detail, sizeof detail, scene == 6 ? "P25 %s" : scene == 7 ? "T100.0 25k %s SQ5" : "DMR TS1 CC1 %s", power);
		fit(df, 0, y, gfx_width(), detail, ui_col.dim);
		y += df->h + (tall ? 20 : 3);
		meter(gfx_height() - df->h, scene == 5 ? -53 : scene == 6 ? -99 : -93, false);
		if (tall) text(df, 0, y + df->h + 4, "S9 = -93 dBm", ui_col.dim);
	}
}
int main(int argc, char **argv)
{
	if (argc != 4) { fprintf(stderr, "usage: displayMockup GEOM POWER OUTPUT_DIR\n"); return 2; }
	const sim_geom_t *g = sim_geom_find(argv[1]);
	assert(g != NULL);
	for (int i = 0; i < 13; i++) {
		assert(count(thresholds[i]) == i + 1);
		assert(count(thresholds[i] - 1) == i);
	}
	assert(count(-142) == 0 && count(-141) == 1 && count(-93) == 9 && count(-73) == 11 && count(-33) == 13);
	const char *names[] = { "dual-fm", "dual-dmr", "dual-p25", "dual-caller", "single-dmr", "long-caller", "unknown-p25", "single-fm", "large-text", "dual-fm-rx", "dual-p25-rx", "dual-two-rx" };
	const char *scripts[] = { "down quick 7", "quick 7", "p25 dlmode:p25 quick 7", "callerdb:on quick 7 wait:3000 tune:a rx:9991234,91,g", "callerdb:on rx:9991234,91,g", "callerdb:on rx:9991234,91,g", "p25 dlmode:p25 p25rx:9995678,7", "down fmrx", "ok 7 2 down down down down down ok long:back quick 7", "down quick 7 tune:a fmrx", "p25 dlmode:p25 quick 7 wait:3000 tune:a p25rx:9991234,7", "receivers:2 quick 7 ctx 3 5 ok wait:3000 rxa:9991234,3100,g rxb:9997777,91,g wait:1000" };
	mkdir(argv[3], 0755);
	char path[512], err[200];
	sim_reset(g, SIM_MAP_K);
	assert(sim_run("callerdb:on", err, sizeof err));
	bool database;
	sim_caller_fixture("Q1ABC Elizabeth Montgomery");
	assert(strcmp(ui_svc_caller_text(RADIO_MODE_DMR, 9991234, &database), "Q1ABC Elizabeth Montgomery") == 0 && database);
	sim_reset(g, SIM_MAP_K);
	assert(sim_run("callerdb:on", err, sizeof err));
	assert(strcmp(ui_svc_caller_text(RADIO_MODE_DMR, 9991234, &database), "Q1ABC Joe Smith") == 0 && database);
	sim_sheet_begin(24, 2);
	for (int i = 0; i < 12; i++) {
		sim_reset(g, SIM_MAP_K);
		if (i == 5) sim_caller_fixture("Q1ABC Elizabeth Montgomery");
		if (!sim_run(scripts[i], err, sizeof err)) { fprintf(stderr, "%s: %s\n", names[i], err); return 1; }
		snprintf(path, sizeof path, "%s/%s-baseline.png", argv[3], names[i]);
		assert(sim_save_png(path, 1));
		char caption[80];
		snprintf(caption, sizeof caption, "%s baseline", names[i]);
		assert(sim_sheet_add(caption));
		for (int y = 0; y < gfx_height(); y++)
			for (int x = 0; x < gfx_width(); x++) baseline[y * gfx_width() + x] = gfx_get_pixel(x, y);
		strip_h = ui_layout.strip_h;
		assert(strip_h <= 40);
		for (int y = 0; y < strip_h; y++)
			for (int x = 0; x < gfx_width(); x++) strip[y * gfx_width() + x] = gfx_get_pixel(x, y);
		proposed(i, argv[2]);
		snprintf(path, sizeof path, "%s/%s-proposed.png", argv[3], names[i]);
		assert(sim_save_png(path, 1));
		snprintf(caption, sizeof caption, "%s proposed", names[i]);
		assert(sim_sheet_add(caption));
	}
	snprintf(path, sizeof path, "%s/comparison.png", argv[3]);
	assert(sim_sheet_save(path));
	sim_reset(g, SIM_MAP_K);
	top("SIGNAL");
	int y = ui_layout.small->h + 4;
	for (int i = 0; i < 3; i++) { meter(y, i == 0 ? -129 : i == 1 ? -93 : -53, false); y += ui_layout.tiny->h + 4; }
	if (gfx_bpp() == 1) {
		snprintf(path, sizeof path, "%s/mono-fill.png", argv[3]); assert(sim_save_png(path, 3));
		top("SIGNAL"); y = ui_layout.small->h + 4;
		for (int i = 0; i < 3; i++) { meter(y, i == 0 ? -129 : i == 1 ? -93 : -53, true); y += ui_layout.tiny->h + 4; }
		snprintf(path, sizeof path, "%s/mono-hollow.png", argv[3]); assert(sim_save_png(path, 3));
	} else { snprintf(path, sizeof path, "%s/meter-states.png", argv[3]); assert(sim_save_png(path, 3)); }
	return 0;
}
