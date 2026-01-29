#ifndef SOUND_ENGINE_H
#define SOUND_ENGINE_H

#include "types.h"

int audio_init(struct player_state* st);
void audio_shutdown(struct player_state* st);

int play_wav
(
	const uint8_t* data_buf,
	size_t buf_len,
	const struct fmt_sub_chunk* fmt
);

int play_wav_player_tick(struct player_state* st);

int convert_wav_to_32(struct player_state** st, const uint8_t* data_buf);
void apply_volume(struct player_state** st);

#endif