// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 David Behnke
// Portable power bytes select the same actual level that firmware resolves, without rewriting imported aliases.
import { test } from "node:test";
import assert from "node:assert/strict";
import { powerProfile, powerByte, powerLevel, radioPowerOptions } from "../static/js/codeplug/power.js";
import { columns, powerOptions } from "../static/js/editor/views.js";
import { CpsSerial } from "../static/js/codeplug/transport.js";
import { FakeCpsRadio } from "../static/js/codeplug/fakecps.js";
import { empty } from "../static/js/codeplug/model.js";
const t = (key) => key;
const profile = powerProfile("power_count=5\npower_default=2\npower_base_names=100mW/1W/5W/25W/45W\npower_bands=136000000-174000000:250mW/1W/5W/25W/50W,400000000-480000000:100mW/1W/5W/25W/45W\n");

test("complete runtime metadata is required; older and malformed replies fall back", () => {
	assert.equal(powerProfile("power_count=5\npower_names=L-/L/M/M+/H"), null);
	assert.equal(powerProfile("power_count=2\npower_base_names=L/H\npower_bands=9-1:L/H"), null);
	assert.equal(powerProfile("power_count=2\npower_base_names=L/H\npower_bands=1-9:L"), null);
	assert.equal(powerOptions(t).length, 11);
	assert.equal(profile.names.length, 5);
});

test("canonical bytes round-trip every supported level for one through ten levels", () => {
	for (let count = 1; count <= 10; count++) {
		for (let level = 0; level < count; level++) {
			const byte = powerByte(level, count);
			assert.ok(byte >= 1 && byte <= 10);
			assert.equal(powerLevel(byte, count), level);
		}
	}
	assert.deepEqual([0, 1, 2, 3, 4].map((level) => powerByte(level, 5)), [1, 3, 6, 8, 10]);
});

test("channel choices use TX-band labels and resolve the configured radio default", () => {
	const vhf = radioPowerOptions(t, profile, { txHz: 145000000 });
	const uhf = radioPowerOptions(t, profile, { txHz: 435000000 });
	assert.deepEqual(vhf[0], [0, "editor.power.default (5W)"]);
	assert.deepEqual(vhf.at(-1), [10, "50W"]);
	assert.deepEqual(uhf.at(-1), [10, "45W"]);
	assert.equal(vhf[1][1], "250mW");
	assert.equal(uhf[1][1], "100mW");
	const col = columns("channels", empty(), t, { power: profile }).find((c) => c.key === "power");
	assert.equal(col.format(10, { power: 10, txHz: 145000000, rxOnly: false }), "50W");
	assert.equal(col.format(10, { power: 10, txHz: 435000000, rxOnly: false }), "45W");
});

test("an imported noncanonical byte remains selected without duplicate level choices", () => {
	for (let byte = 1; byte <= 10; byte++) {
		const options = radioPowerOptions(t, profile, { txHz: 435000000, power: byte });
		assert.equal(options.length, 6);
		assert.equal(options.filter(([value]) => value === byte).length, 1);
		assert.equal(new Set(options.slice(1).map(([, label]) => label)).size, 5);
	}
});

test("mixed-band bulk editing labels both outputs and uses canonical bytes", () => {
	const col = columns("channels", empty(), t, { power: profile }).find((c) => c.key === "power");
	const options = col.bulkOptions([{ txHz: 145000000, power: 2 }, { txHz: 435000000, power: 4 }]);
	assert.deepEqual(options[1], [1, "250mW / 100mW"]);
	assert.deepEqual(options.at(-1), [10, "50W / 45W"]);
});


const testRadios = [{ id: "test", codeplug: { names: ["Test"], base: 0, end: 0xE00000 } }];
const powerView = "section=power\npower_count=2\npower_default=1\npower_base_names=L/H\npower_bands=\n";
const identifyPower = async (views) => {
	const radio = new FakeCpsRadio({ board: "Test", views });
	const tr = new CpsSerial(radio.stream());
	await tr.identify(testRadios);
	await tr.link.s.close();
	return tr.power;
};

test("CPS identification discovers the dedicated power view on main and after fork extras", async () => {
	for (const views of [
		{ 39: { text: powerView } },
		{ 39: { text: "section=dmr_tx_audio\npower_count=9\n", next: 40 }, 40: { text: powerView } },
	]) assert.deepEqual(await identifyPower(views), { names: ["L", "H"], bands: [], defaultLevel: 1 });
});

test("legacy BAD_ARG, unrelated sections and invalid next links keep generic power choices", async () => {
	for (const views of [
		{},
		{ 39: { text: powerView.replace("section=power", "section=other") } },
		{ 39: { text: "section=other\n", next: 39 } },
		{ 39: { text: "section=other\n", next: 0 } },
	]) assert.equal(await identifyPower(views), null);
});

test("optional power discovery stops after eight appended sections", async () => {
	const views = {};
	for (let section = 39; section <= 47; section++) views[section] = { text: section === 47 ? powerView : "section=other\n", next: section + 1 };
	assert.equal(await identifyPower(views), null);
});
