#include "types.h"
#include "sound_engine.h"
#include <limits.h>

static inline int32_t clamp_s32(int64_t v) {
	if (v > INT32_MAX) {
		return INT32_MAX;
	}

	if (v < INT32_MIN) {
		return INT32_MIN;
	}

	return (int32_t) v;
}

int play_wav
(
	const uint8_t* data_buf,
	size_t buf_len,
	const struct fmt_sub_chunk* fmt
)
{
	snd_pcm_t* handle;
	int err;

	if ((err == snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		return -1;
	}

	snd_pcm_set_params(handle,
		SND_PCM_FORMAT_S32_LE,
		SND_PCM_ACCESS_RW_INTERLEAVED,
		fmt->num_channels,
		fmt->sample_rate,
		1, // allows resampling software
		500000 // latency (ms)
	);

	snd_pcm_writei(handle,
		data_buf, buf_len / (fmt->num_channels * fmt->bits_per_sample / 8));

	snd_pcm_drain(handle);
	snd_pcm_close(handle);

	return 0;
}

void apply_volume(struct player_state** st)
{
	if ((*st)->player_gain == 1.0) {
		return;
	}

	size_t total_samples = (*st)->pcm_frames * (*st)->fmt.num_channels;

	for (size_t i = 0; i < total_samples; i++) {
		int64_t v = (int64_t)((*st)->pcm_buf[i] * (*st)->player_gain);
		(*st)->pcm_buf[i] = clamp_s32(v);
	}
}

int convert_wav_to_32(struct player_state** st, const uint8_t* data_buf) {
	size_t bytes_per_sample = (*st)->fmt.bits_per_sample / 8;
	size_t bytes_per_frame = bytes_per_sample * (*st)->fmt.num_channels;
	size_t total_frames = (*st)->buf_len / bytes_per_frame;

	int32_t* pcm_buf = malloc(
		total_frames * (*st)->fmt.num_channels * sizeof(int32_t)
	);

	if (!pcm_buf) {
		return -1;
	}

	const uint8_t* src = data_buf;
	int32_t* dst = pcm_buf;

	for (size_t i = 0; i < total_frames * (*st)->fmt.num_channels; i++) {
		int32_t sample = 0;

		if ((*st)->fmt.bits_per_sample == 8) {
			uint8_t v = *src++;
			sample = ((int32_t)v - 128) << 24;
		} else if ((*st)->fmt.bits_per_sample == 16) {
			int16_t v = (int16_t)(src[0] | (src[1] << 8));
			src += 2;
			sample = ((int32_t) v) << 16;
		} else if ((*st)->fmt.bits_per_sample == 24) {
			int32_t v = (src[0]) | (src[1] << 8) | (src[2] << 16);

			if (v & 0x00800000) {
				v |= 0xFF000000;
			}

			src += 3;
			sample = v << 8;
		} else {			
			free(pcm_buf);
			return -1;
		}

		*dst++ = sample;
	}

	(*st)->pcm_frames = total_frames;
	(*st)->pcm_buf = pcm_buf;
	return 0;
}

int audio_init(struct player_state* st) 
{
	int err;

	if ((err = snd_pcm_open(&st->pcm, "default",
		SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		return -1;
	}

	if ((err = snd_pcm_set_params(
		st->pcm,
		SND_PCM_FORMAT_S32_LE,
		SND_PCM_ACCESS_RW_INTERLEAVED,
		st->fmt.num_channels,
		st->fmt.sample_rate,
		1,
		500000)) < 0) {
		return -1;
	}

	st->mode = PLAYER;
	st->play_state = PLAYING;

	return 0;
}

void audio_shutdown(struct player_state* st) {
	if (!st->pcm) {
		return;
	}

	snd_pcm_drain(st->pcm);
	snd_pcm_close(st->pcm);
	st->pcm = NULL;
	st->mode = COMMAND;
	st->play_state = STOPPED;
}

int play_wav_player_tick(struct player_state* st) {
	if (st->play_state != PLAYING) {
		return -2;
	}

	size_t frames_left = st->pcm_frames - st->cursor;

	if (!frames_left) {
		return 0;
	}

	size_t frames_to_write = 
		frames_left < FRAMES_PER_TICK ? frames_left : FRAMES_PER_TICK;

	snd_pcm_sframes_t written =
		snd_pcm_writei(st->pcm,
			st->pcm_buf + 
				(st->cursor * st->fmt.num_channels),
			frames_to_write);

	if (written < 0) {
		snd_pcm_prepare(st->pcm);
		return -1;
	}

	st->cursor += written;
}