// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Optional P25 labels use DMR's contact/database policy. Cold reads run as UI-posted housekeeping jobs so a
// contact scan never holds up P25 decoding or screen drawing. The UI owns the cache, including negative answers.
#include "config.h"
#if CONFIG_P25_ID_LOOKUP
#include <stdatomic.h>
#include <string.h>
#include "ui_svc.h"
#include "codeplug/contact_name.h"
#include "storage/storage.h"

#define CALLER_CACHE 4
static struct {
	uint32_t id;
	char name[CODEPLUG_NAME_LEN + 1];
} cache[CALLER_CACHE];
static uint8_t next;
static uint32_t serial, generation;
static bool enabled, posted;
// Immutable while posted. done publishes the worker's text; the shared job slot must also be free before reuse.
static struct {
	uint32_t id, serial, generation;
	char name[CODEPLUG_NAME_LEN + 1];
} job;
static atomic_bool done;

static void lookup_job(void *ctx)
{
	(void)ctx;
	contact_name_lookup(job.id, false, job.name);
	atomic_store_explicit(&done, true, memory_order_release);
}

const char *ui_svc_p25_name(uint32_t id)
{
	if (enabled && id != 0)
		for (unsigned i = 0; i < CALLER_CACHE; i++)
			if (cache[i].id == id)
				return cache[i].name;
	return "";
}

bool ui_svc_p25_names_poll(const radio_view_t *v)
{
	bool on = ui_svc_setting_get(UI_SET_P25_ID_LOOKUP) != 0;
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
		if (enabled && job.generation == generation && job.serial == serial) {
			cache[next].id = job.id;
			memcpy(cache[next].name, job.name, sizeof(job.name));
			next = (uint8_t)((next + 1u) % CALLER_CACHE);
			changed = true;
		}
	}
	uint32_t id = v->p25.src;
	if (!enabled || posted || v->ch.mode != RADIO_MODE_P25 ||
	    (v->p25.flags & (RADIO_P25_RX | RADIO_P25_PLAYED)) != (RADIO_P25_RX | RADIO_P25_PLAYED) ||
	    id == 0 || id > 0xFFFFFBu)
		return changed;
	for (unsigned i = 0; i < CALLER_CACHE; i++)
		if (cache[i].id == id)
			return changed;
	if (!storage_job_busy()) {
		job.id = id;
		job.serial = serial;
		job.generation = generation;
		atomic_store_explicit(&done, false, memory_order_relaxed);
		posted = storage_job_post(lookup_job, NULL);
	}
	return changed;
}
#endif
