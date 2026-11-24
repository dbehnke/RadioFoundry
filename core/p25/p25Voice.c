// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 voice: engine frames <-> full-rate decoder <-> PCM blocks (p25Voice.h)
#include "config.h"
#include "dsp/mic_gain.h"
#if CONFIG_P25 && CONFIG_IMBE	// the whole file
#include "p25Voice.h"

#include <stddef.h>
#include <string.h>
#include "dsp/fxp.h"
#include "dsp/tx_level.h"

#define P25_TX_TARGET_RMS	1843u	// -25 dBFS relative to 32768

// the call layer packs what a frame's codes corrected as the decoder reads it
_Static_assert(P25_VOICE_ERRS(11, 2, 1) == IMBE_ERRS(11, 2, 1) && P25_VOICE_ERRS(15, 3, 1) != IMBE_ERRS_LOST,
	       "the voice error byte's layouts differ");

#if (P25_VOICE_QUEUE & (P25_VOICE_QUEUE - 1)) != 0 || P25_VOICE_QUEUE > 128 || P25_VOICE_QUEUE < 2 * P25_VOICE_FRAMES
#error "P25_VOICE_QUEUE must be a power of two up to 128 holding two LDUs"
#endif
#if (P25_VOICE_TX_QUEUE & (P25_VOICE_TX_QUEUE - 1)) != 0 || P25_VOICE_TX_QUEUE > 128
#error "P25_VOICE_TX_QUEUE must be a power of two up to 128"
#endif

#define LOAD(x)		__atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v)	__atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static const hal_audio_t *aud;
static bool streaming;			// radio task only
static p25_voice_mem_t *mem;		// set before the stream starts, cleared after it stops
static imbe_mem_t dec;			// the decoder's pointers into mem
static imbe_enc_mem_t enc;		// the encoder's: the decoder's prediction state and scratch, its own history

// Queue: producer the provider's task (head), consumer the audio callback (tail)
static uint8_t rx_head, rx_tail;
// Call boundaries, written by the provider's task only: a start bumps the sequence, an end copies it
static uint8_t rx_seq, rx_end_seq;

// Transmit. The radio task counts transmissions (odd while one is on); the queue's producer is the audio callback
// (head), its consumer the provider's task (tail). Frames of an earlier transmission are dropped by their count
static uint8_t tx_gen;
static uint8_t tx_head, tx_tail;
static bool mic_agc;
static tx_level_t input_level;
static uint16_t mic_gain_q8 = 256;	// p25VoiceSetMicGain: the mic samples times this / 256 before encoding

// Audio callback state
static uint8_t cb_tx_gen;		// the transmission the encoder runs for (even: none)
static uint8_t cb_seq;
static bool cb_live, cb_primed, cb_playing, cb_speaker, cb_mixing;
// The stream just (re)started: the speaker route is what its last user left, which may be a beep's own stream that
// had it on. The first block sends cb_speaker whatever it was, or the route (and the board's amplifier) stays on
static bool cb_speaker_unsent;
static uint8_t cb_lead;			// silent blocks left before the first frame plays
static uint32_t cb_call_frames;

static p25_voice_stats_t stats;
static p25_voice_mixer_t mixer;

// --- hooks (provider's task) ---

static void rx_start(const p25_call_t *c, void *ctx)
{
	(void)c;
	(void)ctx;
	STORE(rx_seq, (uint8_t)(rx_seq + 1));
}

static void rx_update(const p25_call_t *c, void *ctx)
{
	(void)c;
	(void)ctx;
}

static void rx_voice(const uint8_t imbe[P25_IMBE_BYTES], uint8_t errs, void *ctx)
{
	(void)ctx;
	if (rx_end_seq == rx_seq) {
		rx_start(NULL, NULL);	// frames with no call open: a new call
	}
	stats.rx_in++;
	if (IMBE_ERRS_REPEAT(errs)) {
		stats.rx_bad++;
	} else {
		stats.rx_bit_errors += IMBE_ERRS_TOTAL(errs);
	}
	uint8_t h = rx_head, used = (uint8_t)(h - LOAD(rx_tail));
	if (used >= P25_VOICE_QUEUE) {
		stats.rx_overflows++;
		return;
	}
	if (used + 1u > stats.rx_queue_max) {
		stats.rx_queue_max = used + 1u;
	}
	p25_voice_entry_t *e = &mem->q[h & (P25_VOICE_QUEUE - 1)];
	e->seq = rx_seq;
	e->errs = errs;
	memcpy(e->imbe, imbe, P25_IMBE_BYTES);
	STORE(rx_head, (uint8_t)(h + 1));
}

static void rx_end(void *ctx)
{
	(void)ctx;
	STORE(rx_end_seq, rx_seq);
}

// The encoder's next frame (false: none ready; the engine waits while the air can spare it, then sends silence and
// counts it as filler)
static bool tx_voice(uint8_t imbe[P25_IMBE_BYTES], void *ctx)
{
	(void)ctx;
	uint8_t g = LOAD(tx_gen), t = tx_tail, h = LOAD(tx_head);

	while (t != h && mem->tq[t & (P25_VOICE_TX_QUEUE - 1)].gen != g) {
		t++;	// left over from an earlier transmission
	}
	if (t == h) {
		STORE(tx_tail, t);
		return false;
	}
	memcpy(imbe, mem->tq[t & (P25_VOICE_TX_QUEUE - 1)].imbe, P25_IMBE_BYTES);
	STORE(tx_tail, (uint8_t)(t + 1));
	stats.tx_frames++;
	return true;
}

static const p25_engine_hooks_t hooks = { .rx_start = rx_start, .rx_update = rx_update, .rx_voice = rx_voice,
					   .rx_end = rx_end, .tx_voice = tx_voice };

// --- audio callback (audio task) ---

static void speaker(bool on)
{
	if (cb_speaker != on || LOAD(cb_speaker_unsent)) {
		cb_speaker = on;
		STORE(cb_speaker_unsent, false);
		if (aud != NULL && aud->speaker_pcm != NULL) {
			aud->speaker_pcm(on);
		}
	}
}

static void silence(int16_t *spk)
{
	memset(spk, 0, AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
}

static void rx_block(int16_t *spk)
{
	uint8_t seq = LOAD(rx_seq);
	if (seq != cb_seq) {
		// A new call: the decoder starts over. The speaker route may be off (an end, a stop): sent again in any case
		cb_seq = seq;
		cb_live = true;
		cb_primed = cb_playing = cb_speaker = false;
		cb_lead = 0;
		cb_call_frames = 0;
		stats.rx_peak = 0;
		imbe_dec_init(&dec);
	}
	// Drop entries of earlier calls
	uint8_t t = rx_tail, h = LOAD(rx_head);
	while (t != h && mem->q[t & (P25_VOICE_QUEUE - 1)].seq != cb_seq) {
		t++;
	}
	STORE(rx_tail, t);

	bool ended = LOAD(rx_end_seq) == cb_seq;
	uint8_t queued = (uint8_t)(h - t);
	if (!cb_live) {
		silence(spk);
		return;
	}
	if (!cb_primed) {
		if (queued < P25_VOICE_PRIME && !(ended && queued > 0)) {
			if (ended) {
				cb_live = false;	// a call that ended before playing anything
			}
			silence(spk);
			return;
		}
		cb_primed = cb_playing = true;
		cb_lead = P25_VOICE_LEAD;
		stats.rx_calls++;
		speaker(true);
	}
	if (cb_lead != 0) {
		cb_lead--;
		silence(spk);
		return;
	}
	if (queued == 0) {
		if (ended) {
			cb_live = cb_playing = false;
			speaker(false);
			silence(spk);
			return;
		}
		// Nothing in time: the decoder's bad-frame path (repeat the last good frame, then mute)
		stats.rx_underruns++;
		stats.rx_underrun_at = cb_call_frames;
		stats.rx_concealed++;
		imbe_dec_frame(&dec, imbe_silence, IMBE_ERRS_LOST, spk);
		return;
	}
	const p25_voice_entry_t *e = &mem->q[t & (P25_VOICE_QUEUE - 1)];
	int run = imbe_dec_frame(&dec, e->imbe, e->errs, spk);
	STORE(rx_tail, (uint8_t)(t + 1));
	if (run != 0) {
		stats.rx_concealed++;
		return;
	}
	stats.rx_frames++;
	cb_call_frames++;
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		uint32_t a = (uint32_t)(spk[i] < 0 ? -spk[i] : spk[i]);
		if (a > stats.rx_peak) {
			stats.rx_peak = a;
		}
	}
}

static uint16_t input_block(const int16_t *mic, int16_t *scaled, uint16_t gain, uint16_t target,
			    tx_level_meter_t *input)
{
	if (CONFIG_TX_MIC_LIMITER || target != 0) {
		return tx_level_process(&input_level, mic, scaled, AUDIO_BLOCK_SAMPLES, (uint32_t)gain << 8, target, input);
	}
	if (input != NULL) *input = tx_level_measure(mic, AUDIO_BLOCK_SAMPLES, true);
	input_level.gain_q16 = (uint32_t)gain << 8;
	uint16_t peak = 0;
	for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		int32_t v = fxp_sat16(((int32_t)mic[i] * gain) / 256);
		uint32_t a = (uint32_t)(v < 0 ? -v : v);
		if (a > peak) peak = (uint16_t)a;
		scaled[i] = (int16_t)v;
	}
	return peak;
}


#if CONFIG_P25_PARROT
#define PARROT_FRAMES 500u
static uint8_t (*parrot_frames)[IMBE_BYTES], (*parrot_extra)[IMBE_BYTES];
static unsigned parrot_split;
static uint8_t parrot_state, parrot_command;
static bool parrot_agc, parrot_stop;
static uint8_t parrot_lead;
static uint16_t parrot_target;
static uint16_t parrot_gain_q8;
static uint16_t parrot_progress;
static p25_parrot_view_t parrot_view = { .input_db10 = -960, .encoder_db10 = -960, .output_db10 = -960 };
static uint64_t parrot_power[3];
static uint16_t parrot_count, parrot_played;

static uint8_t *parrot_frame(unsigned n)
{
	return n < parrot_split ? parrot_frames[n] : parrot_extra[n - parrot_split];
}

static void parrot_view_reset(void)
{
	STORE(parrot_view.frames, 0);
	STORE(parrot_view.input_db10, -960);
	STORE(parrot_view.encoder_db10, -960);
	STORE(parrot_view.output_db10, -960);
	STORE(parrot_view.input_peak, 0);
	STORE(parrot_view.encoder_peak, 0);
	STORE(parrot_view.output_peak, 0);
	STORE(parrot_view.gain_q16, TX_LEVEL_UNITY);
	STORE(parrot_view.manual_gain_q16, TX_LEVEL_UNITY);
	STORE(parrot_view.limited_blocks, 0);
	STORE(parrot_view.agc, false);
	STORE(parrot_view.limiter, false);
	STORE(parrot_view.target_dbfs, -25);
	STORE(parrot_view.mic_step, 5);
}

static void parrot_meter(unsigned index, tx_level_meter_t m)
{
	parrot_power[index] += m.power;
	uint16_t *peak = index == 0 ? &parrot_view.input_peak : index == 1 ? &parrot_view.encoder_peak : &parrot_view.output_peak;
	if (m.peak > LOAD(*peak)) STORE(*peak, m.peak);
	unsigned frames = index == 2 ? parrot_played : parrot_count;
	int16_t db = tx_level_db10((uint32_t)(parrot_power[index] / frames));
	if (index == 0) STORE(parrot_view.input_db10, db);
	else if (index == 1) STORE(parrot_view.encoder_db10, db);
	else STORE(parrot_view.output_db10, db);
}

static void parrot_block(const int16_t *mic, int16_t *spk)
{
	uint8_t command = LOAD(parrot_command);
	if (command == 1) {
		uint32_t manual = (uint32_t)parrot_gain_q8 << 8;
		memset(parrot_power, 0, sizeof parrot_power);
		parrot_count = parrot_played = 0;
		imbe_enc_init(&enc);
		tx_level_reset(&input_level, parrot_agc ? TX_LEVEL_UNITY : manual);
		STORE(parrot_command, 0);
	}
	uint8_t state = p25VoiceParrotState();
	if (state == P25_PARROT_RECORD && (parrot_count == PARROT_FRAMES || LOAD(parrot_stop))) {
		imbe_dec_init(&dec);
		STORE(parrot_state, P25_PARROT_PLAY);
		speaker(true);
		parrot_lead = aud != NULL ? (aud->speaker_lead_ms + AUDIO_BLOCK_MS - 1u) / AUDIO_BLOCK_MS : 0;
		// Allow the amplifier to settle before the first decoded block.
		silence(spk);
	} else if (state == P25_PARROT_RECORD) {
		tx_level_meter_t raw;
		speaker(false);
		// The driver consumes this buffer after the callback. Use it as scratch and return silence.
		input_block(mic, spk, parrot_gain_q8, parrot_agc ? parrot_target : 0, &raw);
		imbe_enc_frame(&enc, spk, parrot_frame(parrot_count++));
		parrot_meter(0, raw);
		parrot_meter(1, tx_level_measure(spk, AUDIO_BLOCK_SAMPLES, true));
		STORE(parrot_view.frames, parrot_count);
		STORE(parrot_view.gain_q16, input_level.gain_q16);
		STORE(parrot_view.limited_blocks, input_level.limited_blocks);
		silence(spk);
	} else if (state == P25_PARROT_PLAY && parrot_lead != 0) {
		parrot_lead--;
		silence(spk);
	} else if (state == P25_PARROT_PLAY && parrot_played < parrot_count) {
		imbe_dec_frame(&dec, parrot_frame(parrot_played++), 0, spk);
		parrot_meter(2, tx_level_measure(spk, AUDIO_BLOCK_SAMPLES, true));
	} else {
		silence(spk);
		// The driver still has audio to play. Leave the route open and let the radio task detect a stalled drain.
		if (state == P25_PARROT_PLAY && aud != NULL && aud->spk_pending != NULL && aud->spk_pending()) return;
		speaker(false);
		if (state == P25_PARROT_PLAY) STORE(parrot_state, P25_PARROT_DONE);
	}
	if (state == P25_PARROT_RECORD || state == P25_PARROT_PLAY) {
		STORE(parrot_progress, (uint16_t)(LOAD(parrot_progress) + 1u));
	}
}
#endif

// One mic block into the transmission g: encoded and queued for the engine
static void tx_block(uint8_t g, const int16_t *mic, int16_t *scaled)
{
	uint32_t limited = input_level.limited_blocks;
	uint16_t target = CONFIG_P25_TX_AGC && LOAD(mic_agc) ? P25_TX_TARGET_RMS : 0;
	uint16_t peak = input_block(mic, scaled, LOAD(mic_gain_q8), target, NULL);
	stats.tx_gain_q16 = input_level.gain_q16;
	stats.tx_limited_blocks += input_level.limited_blocks - limited;
	if (peak > stats.tx_mic_peak) {
		stats.tx_mic_peak = peak;
	}
	uint8_t h = tx_head;
	if ((uint8_t)(h - LOAD(tx_tail)) >= P25_VOICE_TX_QUEUE) {
		// The engine is not taking frames (not started yet, or ending): the encoder still runs, to keep its history
		uint8_t drop[P25_IMBE_BYTES];

		imbe_enc_frame(&enc, scaled, drop);
		stats.tx_encoded++;
		stats.tx_dropped++;
		return;
	}
	p25_voice_tx_entry_t *e = &mem->tq[h & (P25_VOICE_TX_QUEUE - 1)];
	e->gen = g;
	imbe_enc_frame(&enc, scaled, e->imbe);
	stats.tx_encoded++;
	if (memcmp(e->imbe, imbe_silence, P25_IMBE_BYTES) == 0) {
		stats.tx_silent++;
	}
	STORE(tx_head, (uint8_t)(h + 1));
}

void p25VoiceAudioBlock(const int16_t *mic, int16_t *spk, void *ctx)
{
	(void)ctx;
	if (LOAD(mem) == NULL) {
		silence(spk);
		return;
	}
#if CONFIG_P25_PARROT
	if (LOAD(parrot_state) != P25_PARROT_OFF) {
		parrot_block(mic, spk);
		return;
	}
#endif
	uint8_t g = LOAD(tx_gen);
	if ((g & 1u) != 0) {
		if (g != cb_tx_gen) {
			cb_tx_gen = g;
			stats.tx_mic_peak = 0;
			stats.tx_limited_blocks = 0;
			imbe_enc_init(&enc);
			uint32_t initial = CONFIG_P25_TX_AGC && LOAD(mic_agc) ? TX_LEVEL_UNITY : (uint32_t)LOAD(mic_gain_q8) << 8;
			tx_level_reset(&input_level, initial);
		}
		// The speaker block doubles as the encoder input scratch while transmitting.
		tx_block(g, mic, spk);
		silence(spk);
	} else {
		if ((cb_tx_gen & 1u) != 0) {
			// The encoder used the decoder's prediction state: a call still coming in starts from none
			cb_tx_gen = g;
			imbe_dec_init(&dec);
		}
		rx_block(spk);
	}
	p25_voice_mixer_t mix = LOAD(mixer);
	if (mix != NULL && mix(spk)) {
		cb_mixing = true;
		speaker(true);
	} else if (cb_mixing) {
		cb_mixing = false;
		if (!cb_playing) {
			speaker(false);
		}
	}
	if (LOAD(cb_speaker_unsent)) {
		speaker(cb_speaker);
	}
}

// --- radio task ---

static void stream_start(void)
{
	STORE(cb_speaker_unsent, true);	// before the callback can run
	streaming = aud != NULL && aud->start != NULL && aud->start(p25VoiceAudioBlock, NULL);
}

#if CONFIG_P25_PARROT
void p25VoiceParrotReady(uint8_t (*frames)[IMBE_BYTES], unsigned count, uint8_t (*extra)[IMBE_BYTES])
{
	if (aud != NULL) {
		if (aud->stop != NULL) aud->stop();
		if (aud->speaker_pcm != NULL) aud->speaker_pcm(false);
	}
	streaming = false;
	parrot_frames = frames;
	parrot_split = count;
	parrot_extra = extra;
	STORE(parrot_stop, false);
	STORE(parrot_progress, 0);
	parrot_view_reset();
	STORE(parrot_state, P25_PARROT_READY);
	STORE(parrot_command, 0);
}
bool p25VoiceParrotCapture(bool agc, int8_t target_dbfs, uint8_t mic_step, bool hardware_gain)
{
	uint8_t state = p25VoiceParrotState();
	if (state != P25_PARROT_READY && state != P25_PARROT_DONE && state != P25_PARROT_ERROR) return false;
	if (LOAD(mem) == NULL) return false;
	if (streaming) { aud->stop(); streaming = false; } // synchronizes with the audio callback before changing its parameters
	static const uint16_t rms[] = { 583, 654, 734, 823, 924, 1036, 1163, 1305, 1464, 1642, 1843, 2068, 2320, 2603, 2920, 3277, 3677, 4125, 4629, 5193, 5827 };
	int target = target_dbfs < -35 ? -35 : target_dbfs > -15 ? -15 : target_dbfs;
	parrot_agc = agc;
	parrot_target = rms[target + 35];
	parrot_gain_q8 = mic_gain_step_q8(hardware_gain ? 5 : mic_step);
	parrot_view_reset();
	STORE(parrot_view.agc, agc);
	STORE(parrot_view.limiter, agc || CONFIG_TX_MIC_LIMITER);
	STORE(parrot_view.target_dbfs, (int8_t)target);
	STORE(parrot_view.mic_step, mic_step > 10 ? 10 : mic_step);
	STORE(parrot_view.manual_gain_q16, (uint32_t)parrot_gain_q8 << 8);
	STORE(parrot_stop, false);
	STORE(parrot_progress, 0);
	STORE(parrot_command, 1);
	STORE(parrot_state, P25_PARROT_RECORD);
	stream_start();
	if (!streaming) p25VoiceParrotFail();
	return streaming;
}
void p25VoiceParrotStop(void) { STORE(parrot_stop, true); }
void p25VoiceParrotFail(void)
{
	if (streaming) {
		aud->stop();
		streaming = false;
	}
	if (aud != NULL && aud->speaker_pcm != NULL) aud->speaker_pcm(false);
	STORE(parrot_command, 0);
	STORE(parrot_state, P25_PARROT_ERROR);
}

uint8_t p25VoiceParrotState(void) { return LOAD(parrot_state); }
uint16_t p25VoiceParrotProgress(void) { return LOAD(parrot_progress); }

void p25VoiceParrotView(p25_parrot_view_t *out)
{
	out->frames = LOAD(parrot_view.frames);
	out->input_db10 = LOAD(parrot_view.input_db10);
	out->encoder_db10 = LOAD(parrot_view.encoder_db10);
	out->output_db10 = LOAD(parrot_view.output_db10);
	out->input_peak = LOAD(parrot_view.input_peak);
	out->encoder_peak = LOAD(parrot_view.encoder_peak);
	out->output_peak = LOAD(parrot_view.output_peak);
	out->gain_q16 = LOAD(parrot_view.gain_q16);
	out->manual_gain_q16 = LOAD(parrot_view.manual_gain_q16);
	out->limited_blocks = LOAD(parrot_view.limited_blocks);
	out->agc = LOAD(parrot_view.agc);
	out->limiter = LOAD(parrot_view.limiter);
	out->target_dbfs = LOAD(parrot_view.target_dbfs);
	out->mic_step = LOAD(parrot_view.mic_step);
	out->state = p25VoiceParrotState();
}
#endif

void p25VoiceInit(const hal_audio_t *audio)
{
	aud = audio;
}

void p25VoiceBegin(p25_voice_mem_t *m)
{
#if CONFIG_P25_PARROT
	STORE(parrot_state, P25_PARROT_OFF);
#endif
	// The stream is stopped (p25VoiceEnd) and the engine not started: nothing else touches this state now
	rx_head = rx_tail = 0;
	rx_seq = rx_end_seq = 0;
	tx_head = tx_tail = 0;
	tx_gen = cb_tx_gen = 0;
	cb_seq = 0;
	cb_live = cb_primed = cb_playing = cb_speaker = cb_mixing = false;
	dec.s = &m->st;
	dec.p = &m->pr;
	dec.mbe.s = &m->ms;
	dec.mbe.prev = &m->prev;
	dec.mbe.cur = &m->cur;
	dec.mbe.buf = m->buf;
	imbe_dec_init(&dec);
	enc.s = &m->st.q;
	enc.est = &m->est;
	enc.p = &m->pr;
	enc.buf = m->buf;
	STORE(mem, m);
}

void p25VoicePoll(void)
{
	if (!streaming && aud != NULL && LOAD(mem) != NULL && LOAD(rx_seq) != 0) {
		stream_start();
	}
}

void p25VoiceTx(bool on)
{
	if (LOAD(mem) == NULL || on == ((tx_gen & 1u) != 0)) {
		return;
	}
	STORE(tx_gen, (uint8_t)(tx_gen + 1));
	if (on && !streaming && aud != NULL) {
		stream_start();
	}
}

void p25VoiceSetAgc(bool on) { STORE(mic_agc, on); }

void p25VoiceSetMicGain(uint8_t step)
{
	STORE(mic_gain_q8, mic_gain_step_q8(step));
}

void p25VoiceEnd(void)
{
	if (streaming && aud != NULL) {
		aud->stop();
		if (aud->speaker_pcm != NULL) {
			aud->speaker_pcm(false);
		}
	}
	streaming = false;
#if CONFIG_P25_PARROT
	STORE(parrot_state, P25_PARROT_OFF);
	if (parrot_frames != NULL) memset(parrot_frames, 0, parrot_split * IMBE_BYTES);
	if (parrot_extra != NULL) memset(parrot_extra, 0, (PARROT_FRAMES - parrot_split) * IMBE_BYTES);
	parrot_frames = parrot_extra = NULL;
#endif
	STORE(mem, (p25_voice_mem_t *)NULL);
}

bool p25VoiceStreaming(void)
{
	return streaming;
}

void p25VoiceSetMixer(p25_voice_mixer_t m)
{
	STORE(mixer, m);
}

const p25_engine_hooks_t *p25VoiceHooks(void)
{
	return &hooks;
}

void p25VoiceGetStats(p25_voice_stats_t *out)
{
	*out = stats;
}

void p25VoiceResetStats(void)
{
	memset(&stats, 0, sizeof(stats));
}

#endif	// CONFIG_P25 && CONFIG_IMBE
