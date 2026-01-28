#include "types.h"
#include "sound_engine.h"

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
		SND_PCM_FORMAT_S16_LE,
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

int audio_init(struct player_state* st) 
{
	int err;

	if ((err = snd_pcm_open(&st->pcm, "default",
		SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		return -1;
	}

	if ((err = snd_pcm_set_params(
		st->pcm,
		SND_PCM_FORMAT_S16_LE,
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
	if (st->play_state == PAUSED) {
		return -2;
	}

	size_t bytes_per_frame = 
		st->fmt.num_channels * (st->fmt.bits_per_sample / 8);

	size_t frames_left =
		(st->buf_len - st->cursor) / bytes_per_frame;

	if (!frames_left) {
		return 0;
	}

	size_t frames_to_write = 
		frames_left < FRAMES_PER_TICK ? frames_left : FRAMES_PER_TICK;

	snd_pcm_sframes_t written =
		snd_pcm_writei(st->pcm,
			st->data_buf + st->cursor,
			frames_to_write);

	if (written < 0) {
		snd_pcm_prepare(st->pcm);
		return -1;
	}

	st->cursor += written * bytes_per_frame;
}