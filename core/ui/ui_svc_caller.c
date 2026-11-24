// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Caller labels share DMR's contact/database policy. Cold reads run as UI-posted housekeeping jobs so a
// contact scan never holds up P25 decoding or screen drawing. The UI owns the cache, including negative answers.
#include "config.h"
#if CONFIG_DMR && (CONFIG_CALLER_DETAILS || CONFIG_P25_ID_LOOKUP)
#include <stdatomic.h>
#include <string.h>
#include "ui_svc.h"
#include "codeplug/contact_name.h"
#include "storage/storage.h"

#define CALLER_CACHE 4
static struct {
	uint32_t id;
	char name[DMR_IDDB_TEXT_MAX + 1];
	bool database;
} cache[CALLER_CACHE];
static uint8_t next;
static uint32_t serial, generation;
static bool enabled, posted;
// Immutable while posted. done publishes the worker's text; the shared job slot must also be free before reuse.
static struct {
	uint32_t id, serial, generation;
	char name[DMR_IDDB_TEXT_MAX + 1];
	bool database;
} job;
static atomic_bool done;

static void lookup_job(void *ctx)
{
	(void)ctx;
	job.database = contact_text_lookup(job.id, false, job.name, sizeof job.name);
	atomic_store_explicit(&done, true, memory_order_release);
}

const char *ui_svc_caller_text(uint8_t mode, uint32_t id, bool *database)
{
	*database = false;
	if ((mode != RADIO_MODE_DMR && (mode != RADIO_MODE_P25 || !enabled)) ||
	    (mode == RADIO_MODE_P25 && id > 0xFFFFFBu))
		return "";
	if (id != 0)
		for (unsigned i = 0; i < CALLER_CACHE; i++)
			if (cache[i].id == id) {
				*database = cache[i].database;
				return cache[i].name;
			}
	return "";
}

const char *ui_svc_p25_name(uint32_t id)
{
	bool database;
	return ui_svc_caller_text(RADIO_MODE_P25, id, &database);
}

bool ui_svc_caller_poll(const radio_view_t *v)
{
	bool on = CONFIG_P25_ID_LOOKUP && ui_svc_setting_get(UI_SET_P25_ID_LOOKUP) != 0;
	uint32_t revision = codeplug_serial();
	bool changed = false;
	if (on != enabled || revision != serial) {
		enabled = on;
		serial = revision;
		generation++;
		memset(cache, 0, sizeof(cache));
		next = 0;
		changed = true;
	}
	if (posted && atomic_load_explicit(&done, memory_order_acquire) && !storage_job_busy()) {
		posted = false;
		if (job.generation == generation && job.serial == serial) {
			cache[next].id = job.id;
			memcpy(cache[next].name, job.name, sizeof(job.name));
			cache[next].database = job.database;
			next = (uint8_t)((next + 1u) % CALLER_CACHE);
			changed = true;
		}
	}
	uint32_t id = 0;
#if CONFIG_P25
	if (enabled && v->ch.mode == RADIO_MODE_P25 &&
	    (v->p25.flags & (RADIO_P25_RX | RADIO_P25_PLAYED)) == (RADIO_P25_RX | RADIO_P25_PLAYED) &&
	    v->p25.src <= 0xFFFFFBu)
		id = v->p25.src;
#endif
#if CONFIG_CALLER_DETAILS
	if (v->ch.mode == RADIO_MODE_DMR && v->dmr.state == RDMR_RX)
		id = v->dmr.call.src;
#endif
	ui_svc_caller_request(v->ch.mode, id);
	return changed;
}

void ui_svc_caller_request(uint8_t mode, uint32_t id)
{
	if ((mode != RADIO_MODE_DMR || !CONFIG_CALLER_DETAILS) &&
	    (mode != RADIO_MODE_P25 || !enabled || id > 0xFFFFFBu))
		return;
	if (posted || id == 0 || id > 0xFFFFFFu)
		return;
	for (unsigned i = 0; i < CALLER_CACHE; i++)
		if (cache[i].id == id)
			return;
	if (!storage_job_busy()) {
		job.id = id;
		job.serial = serial;
		job.generation = generation;
		atomic_store_explicit(&done, false, memory_order_relaxed);
		posted = storage_job_post(lookup_job, NULL);
	}
	return;
}
#endif
