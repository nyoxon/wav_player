/*
simple wav player using ALSA (Advance Linux Sound Architecture)

INTRO:

in this player, i'm copying to program memory all the relevant data information
from the .wav that is actualy playing, which is not a great ideia
for efficiency, but the ideia is to get used to this world, then 
get things a little bit faster later using dynamic reading with mmap



HOW TO COMPILE:

gcc -o player player.c fd_handle.c sound_engine.c types.c cli_interface.c -lasound

ALSA is the default sound layer on modern linux
it exposes audio devices in /dev/snd/, but it's not
trivial to write directly to them because the hardware
needs control over audio buffers, triggers and formats
that's why there's 'libasound' (alsa/asoundlib.h), which
encapsulates all this complexity and allows you to write
relatively simple code



the simple flow for playing audio is as follows:

- open playback device ("default", "hw:0,0" etc) using snd_pcm_open()
- configure PCM parameters using snd_pcm_set_params()
(
	- sample format (SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_U8)
	- numb of channels (mono = 1, stereo = 2)
	- sample rate (usually 44100 hz)
	- access type 
	(
		- SND_PCM_ACCESS_RW_INTERLEAVED
		each frame contains one sample of all channels, in sequence
		for stereo, the order is L R L R ...
		(wav files are usually armazened interleaved)
		buffer: [L R L R L R ...]

		- SND_PCM_ACCESS_RW_NONINTERLEAVED
		each channel has its own sample buffer
		for stereo, you'd have two buffers:
		buffer1[]: L1, L2, L3 ...
		buffer2[]: R1, R2, R3 ...
		each buffer is sent separately and each call writes a number of
		frames per buffer
		(it's usually used to handle each channel separately: mixers,
		effects etc)
		buffer: [L L L ...] [R R R ...]
	)
)
- write audio frames on device using snd_pcm_writei() (INTERLEAVED) 
or snd_pcm_writen() (NONINTERLEAVED)
- "drain" the device, ensuring that all pending audio is played using
snd_pcm_drain()
- close the device snd_pcm_close()



how the wav data is read:

each frame sent to snd_pcm_writei() must have a sample per channel

example using WAV 16-bit stereo:
audio buffer: L1 R1 L2 R2 L3 R3 ...
Frames:
Frame 1 = L1 + R1 = 4 bytes
Frame 2 = L2 + R2 = 4 bytes
...
num of frames = data_buffer_length / frame_size
frame_size = num_channels * bits_per_sample / 8
ex: stereo 16-bit
num_channels = 2
bits_per_sample = 16
frame_size = 2 * 16 / 8 = 4 bytes 


additional concepts:

sample = audio value at a specific instant of time
ex: in a 16-bit WAV, each sample has 2 bytes (uint16_t) that
represents the amplitude of the sound wave at a specific instante

channels = independent audio lines
ex: mono -> 1 channel -> 1 sample per instant

frame = a sample of all channels at the same instant
ex: mono -> frame = 1 sample
ex: stereo 16-bit -> frame = 2 samples (stereo) * 2 bytes (16-bit) = 4 bytes

PCM (Pulse Code Modulation) = most basic format of digital audio
there is no compression and each sample is armazened using 8, 16, 24 or 32 bits:
- not having compressing means that the data_buf size is exactly equals to
subchunk2_size
- each complete frame is a multiple of frame_size
- reading and writing is simple
- INTERLEAVED by default

additional functions:
snd_strerror(int err) -> converts ALSA error codes to human readable format

*/

#include "types.h"
#include "fd_handle.h"
#include "sound_engine.h"
#include "cli_interface.h"
#include <string.h>

volatile sig_atomic_t should_exit = 0;

void handle_signal(int sig) {
	(void) sig;
	should_exit = 1;
}

void add_track(const char* path, const char* fullname, void* userdata) {
	struct player_state* st = (struct player_state*) userdata;

	if (!st) {
		return;
	}

	struct track t;
	t.path = strdup(path);
	t.name = strdup(fullname);

	struct fmt_sub_chunk fmt;
	struct data_sub_chunk data;

	int file = read_wav_from_filename(path,
		NULL, &fmt, &data, NULL, NULL, NULL);

	if (file < 0) {
		fprintf(stderr, "reading wav failed\n");
		return;
	}

	close(file);

	double duration = (double) (data.subchunk2_size / fmt.byte_rate);
	t.duration = duration;

	if (playlist_push(&st->playlist, t) < 0) {
		if (st->playlist.len > 0) {
			playlist_free(&st->playlist);

			free(t.path);
			free(t.name);
		}
	}
}

void create_playlist(const char* path, int recursive, struct player_state* st) {
	playlist_init(&st->playlist);
	list_wavs(path, recursive, add_track, st);
}

int init
(
	const char* path,
	int recursive, 
	struct player_state *st
) 
{
	if (!st) {
		fprintf(stderr, "st is NULL\n");
		return -1;
	}

	st->running = 1;
	snprintf(st->dir_path, PATH_MAX_LENGTH, "%s", path);
	st->recursive = recursive;
	st->playlist_loop = 0;
	st->track_loop = 0;
	st->played = 0;
	st->mode = COMMAND;
	st->play_state = STOPPED;
	st->player_gain = 1.0; // default
	st->played = 0;

	create_playlist(st->dir_path, recursive, st);
	st->current_track = 0;
	st->cursor = 0;

	st->pcm = NULL;

	return 0;
}

int main(int argc, const char* argv[]) {
	struct sigaction sa = {0};
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	// --- READING .WAV ---
	char path[PATH_MAX_LENGTH];
	int recursive = 1;

	if (argc == 1) {
		snprintf(path, PATH_MAX_LENGTH, "%s", ".");
	} else if (argc == 2) {
		snprintf(path, PATH_MAX_LENGTH, "%s", argv[1]);
	} else if (argc == 3) {
		snprintf(path, PATH_MAX_LENGTH, "%s", argv[1]);
		recursive = atoi(argv[2]);
	} else {
		printf("usage: %s [PATH] [RECURSIVE]\n", argv[0]);
		printf("if [PATH] (relative or global) is omitted, then the directory\n");
		printf("that will be used by the player will be the current directory ./\n");
		printf("[RECURSIVE] must be 1 if you want the program to read the\n");
		printf("directory recursively (default) or 0 otherwise\n");
		return -1;
	}

	struct player_state st = {0};

	int ret = init(path, recursive, &st);

	if (ret < 0) {
		fprintf(stderr, "reading dir failed\n");
		return -1;
	}

	while (should_exit == 0 && st.running == 1) {
		command_loop(&st, &should_exit);
		player_loop(&st, &should_exit);
	}

	playlist_free(&st.playlist);

	return 0;
}

