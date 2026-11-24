// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Real deferred caller service and shared DMR policy, with controllable flash jobs and synthetic records.
#include <stdio.h>
#include <string.h>
#include "ui/ui_svc.h"
#include "codeplug/contact_name.h"
#include "storage/storage.h"

static unsigned checks, fails, contacts, database;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static uint32_t revision;
static bool enabled, busy;
static storage_job_fn pending;
static radio_view_t view;

uint32_t codeplug_serial(void) { return revision; }
int32_t ui_svc_setting_get(uint8_t id) { return id == UI_SET_P25_ID_LOOKUP && enabled; }
bool storage_job_busy(void) { return busy; }
bool storage_job_post(storage_job_fn fn, void *ctx)
{
	(void)ctx;
	if (busy) return false;
	busy = true;
	pending = fn;
	return true;
}
bool codeplug_contact_find(uint32_t id, bool group, contact_t *out)
{
	contacts++;
	if (((id == 100 || id == 0xFFFFFCu) && !group) || (id == 200 && group)) {
		memset(out, 0, sizeof(*out));
		strcpy(out->name, group ? "Group only" : "Local caller");
		return true;
	}
	return false;
}
bool dmr_iddb_lookup(uint32_t id, char *out, size_t len)
{
	database++;
	if (id != 100 && id != 200 && id != 800) return false;
	snprintf(out, len, id == 800 ? "Q1ABC Joe Smith Springfield Massachusetts" : "DB caller %u", (unsigned)id);
	return true;
}
static void poll(void) { ui_svc_caller_poll(&view); }
static void finish(void)
{
	CHECK(pending != NULL);
	pending(NULL);
	pending = NULL;
	// Worker publication alone cannot release the storage slot or allow another request to overwrite the result.
	poll();
	CHECK(busy);
	busy = false;
	poll();
}
static void caller(uint32_t id)
{
	view.p25.src = id;
	poll();
}
static bool named(uint32_t id, const char *name) { return strcmp(ui_svc_p25_name(id), name) == 0; }

int main(void)
{
	char name[CODEPLUG_NAME_LEN + 1];
	view.ch.mode = RADIO_MODE_P25;
	view.p25.flags = RADIO_P25_RX | RADIO_P25_PLAYED;
	caller(100);
	CHECK(!busy && contacts == 0 && database == 0 && named(100, ""));
	enabled = true;
	caller(0);
	CHECK(!busy);
	caller(100);
	CHECK(busy && contacts == 0 && named(100, ""));
	finish();
	CHECK(named(100, "Local caller") && database == 0);
	unsigned reads = contacts;
	for (unsigned i = 0; i < 100; i++) poll();
	CHECK(contacts == reads && !busy);
	caller(200);
	finish();
	CHECK(named(200, "DB caller 200")); // group contact 200 cannot identify a caller
	caller(300);
	finish();
	reads = contacts;
	for (unsigned i = 0; i < 100; i++) poll();
	CHECK(contacts == reads && named(300, "")); // negative answer cached

	// A caller changes while a read is queued: its completed name never labels the new ID.
	caller(400);
	caller(100);
	CHECK(named(100, "Local caller"));
	finish();
	CHECK(named(100, "Local caller") && named(400, ""));
	caller(500); finish();
	caller(100); // oldest of four answers was evicted
	CHECK(busy);
	finish();

	// A preference change invalidates pending results, including Off -> On before completion.
	caller(600);
	enabled = false; poll();
	CHECK(named(100, ""));
	enabled = true; caller(0);
	finish();
	CHECK(named(600, ""));
	caller(600); CHECK(busy); finish();

	// Replacing a codeplug invalidates both hits and misses; a busy unrelated job gets retry, not replacement.
	busy = true; revision++;
	caller(100);
	CHECK(pending == NULL && named(100, ""));
	busy = false; poll(); CHECK(pending != NULL); finish();
	CHECK(named(100, "Local caller"));
	caller(200); revision++; caller(0); finish();
	CHECK(named(100, "") && named(200, ""));
	caller(200); finish(); CHECK(named(200, "DB caller 200"));

	reads = contacts;
	view.ch.mode = RADIO_MODE_NXDN; caller(700);
	view.ch.mode = RADIO_MODE_P25; view.p25.flags = RADIO_P25_RX; caller(700);
	view.p25.flags |= RADIO_P25_PLAYED;
	caller(0xFFFFFCu); caller(0xFFFFFFu); caller(0x1000000u);
	CHECK(!busy && contacts == reads);
	contact_name_lookup(100, false, name); CHECK(strcmp(name, "Local caller") == 0);
	unsigned db_reads = database;
	contact_name_lookup(200, true, name); CHECK(strcmp(name, "Group only") == 0 && database == db_reads);
	contact_name_lookup(300, true, name); CHECK(name[0] == 0 && database == db_reads);
	// DMR needs the complete database label even while the P25 preference is off.
	enabled = false;
	view.ch.mode = RADIO_MODE_DMR;
	view.dmr.state = RDMR_RX;
	view.dmr.call.src = 800;
	poll(); finish();
	bool from_database;
	CHECK(strcmp(ui_svc_caller_text(RADIO_MODE_DMR, 800, &from_database),
		"Q1ABC Joe Smith Springfield Massachusetts") == 0 && from_database);
	CHECK(named(800, ""));
	view.dmr.call.src = 100;
	poll(); finish();
	CHECK(strcmp(ui_svc_caller_text(RADIO_MODE_DMR, 100, &from_database), "Local caller") == 0 && !from_database);
	// A second receiver queues its own identity; cache hits and unsupported modes never read flash.
	ui_svc_caller_request(RADIO_MODE_DMR, 800);
	CHECK(!busy);
	ui_svc_caller_request(RADIO_MODE_DMR, 200);
	CHECK(busy); finish();
	CHECK(strcmp(ui_svc_caller_text(RADIO_MODE_DMR, 200, &from_database), "DB caller 200") == 0);
	ui_svc_caller_request(RADIO_MODE_P25, 700);
	ui_svc_caller_request(RADIO_MODE_FM, 700);
	CHECK(!busy);
	// Sharing the cache never turns a reserved P25 unit ID into a caller label.
	ui_svc_caller_request(RADIO_MODE_DMR, 0xFFFFFCu);
	finish();
	view.dmr.call.src = 0xFFFFFCu;
	enabled = true; poll(); // changing the policy invalidates this entry
	ui_svc_caller_request(RADIO_MODE_DMR, 0xFFFFFCu);
	finish();
	CHECK(strcmp(ui_svc_caller_text(RADIO_MODE_DMR, 0xFFFFFCu, &from_database), "Local caller") == 0);
	CHECK(named(0xFFFFFCu, ""));
	printf("callerNameTest: %u checks, %u failures\n", checks, fails);
	return fails != 0;
}
