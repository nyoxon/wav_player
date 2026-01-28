#include "cli_interface.h"
#include "fd_handle.h"
#include "sound_engine.h"
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

static int is_wav(const char* name) {
	const char* dot = strrchr(name, '.');

	if (!dot) {
		return 0;
	}

	return strcasecmp(dot, ".wav") == 0;
}

void list_wavs
(
	const char* path,
	int recursive,
	void (*on_wav) (const char* fullpath, const char* fullname, void* userdata),
	void* userdata
)
{
	DIR* dir = opendir(path); // a pointer to the beggining of the dir

	if (!dir) {
		return;
	}

	struct dirent* ent;

	while ((ent = readdir(dir))) { // increments the pointer
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}

		char fullpath[PATH_MAX_LENGTH];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);

		struct stat st;

		if (stat(fullpath, &st) != 0) {
			continue;
		}

		if (S_ISDIR(st.st_mode) && recursive) { // actual file is a dir?
			list_wavs(fullpath, recursive, on_wav, userdata);
		} else if (S_ISREG(st.st_mode) && is_wav(ent->d_name)) {
			on_wav(fullpath, ent->d_name, userdata);
		}
	}

	closedir(dir);
}

struct track* get_current_music(struct player_state* st) {
	return &st->playlist.items[st->current_track];
}

struct track* get_nth_music(struct player_state* st, size_t index) {
	if (index >= st->playlist.len) {
		fprintf(stderr, "index out of bounds\n");
		return NULL;
	}

	return &st->playlist.items[index];
}

int set_current_music(struct player_state* st, size_t index) {
	if (index >= st->playlist.len) {
		fprintf(stderr, "index out of bounds\n");
		return -1;
	}

	struct track* t = get_nth_music(st, index);

	struct read_wav_result read_result = {0};
	struct fmt_sub_chunk fmt;
	uint8_t* data_buf;
	size_t buf_len;

	int file = read_wav_from_filename(t->path, NULL, &fmt, NULL,
		&data_buf, &buf_len, &read_result);

	if (file < 0) {
		fprintf(stderr, "reading wav failed\n");
		return -1;
	}

	close(file);

	if (!read_result.fmt) {
		fprintf(stderr, "reading wav failed\n");
		return -1;
	}

	st->fmt = fmt;
	st->data_buf = data_buf;
	st->buf_len = buf_len;
	st->current_track = index;
	st->cursor = 0;

	return 0;
}

void next_music(struct player_state* st) {
	st->played++;

	if (st->track_loop) {
		st->cursor = 0;
		return;
	}

	if (st->current_track >= st->playlist.len - 1) {
		if (st->playlist_loop) {
			if (st->playlist.len == 1) {
				st->cursor = 0;
				return;
			} else {
				if (set_current_music(st, 0) < 0) {
					fprintf(stderr, "playing wav failed\n");
				}

				return;
			}
		} else {
			st->mode = COMMAND;
			st->play_state = STOPPED;
			st->cursor = 0;
		}
	}

	if (set_current_music(st, st->current_track + 1) < 0) {
		fprintf(stderr, "playing wav failed\n");
		st->mode = COMMAND;
		st->play_state = STOPPED;
	}
}

void print_help() {
	printf("\ncommands:\n\n");
	printf("(play number_track) -> play track of number number_track\n");
	printf("(play) -> (play 0)\n");
	printf("(list) -> list all wav files\n");
	printf("(loop) -> enable/disable playlist loop\n");
	printf("(help) -> list all possible commands\n");
	printf("(quit) -> quit the program\n\n");
	printf("if you add a new WAV in the directory, restart the program\n");
}

void process_command_input(char* line, struct player_state* st) {
	char cmd[16];
	int flag;

	int count = sscanf(line, "%15s %d", cmd, &flag);

	if (strncmp(cmd, "quit", 4) == 0) {
		st->running = 0;
	} else if (strncmp(cmd, "help", 4) == 0) {
		print_help();
	} else if (strncmp(cmd, "list", 4) == 0) {
		playlist_print(&st->playlist);
	} else if (strncmp(cmd, "play", 4) == 0) {
		if (count == 1) {
			if (set_current_music(st, 0) < 0) {
				fprintf(stderr, "playing wav failed\n");
				return;
			}
		} else if (count == 2) {
			if (flag < 1 || flag > (int) st->playlist.len) {
				fprintf(stderr, "playing wav failed\n");
				return;
			}

			if (set_current_music(st, flag - 1) < 0) {
				fprintf(stderr, "playing wav failed\n");
				return;		
			}
		}

		if (audio_init(st) < 0) {
			fprintf(stderr, "playing wav failed\n");
			return;
		}
	} else if (strcmp(cmd, "loop") == 0) {
		st->playlist_loop = (st->playlist_loop) ? 0 : 1;

		if (st->playlist_loop) {
			printf("playlistloop: enabled\n");
		} else {
			printf("playlistloop: disabled\n");
		}
	} else if (*cmd) {
		printf("\ninvalid command: %s\n", cmd);
		printf("(help) for possible commands\n");
	}
}

void command_loop(struct player_state* st, volatile sig_atomic_t* should_exit) {
	char line[256];
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

	while (st->running && (st->mode == COMMAND)) {
		if (*should_exit) {
			st->running = 0;
		}

		printf("> ");
		fflush(stdout);


		if (!fgets(line, sizeof(line), stdin)) {
			break;
		}

		process_command_input(line, st);
	}
}

static void process_key(struct player_state* st, char c) {
	if (c == ' ') {
		st->play_state = (st->play_state == PAUSED) ? PLAYING : PAUSED;
		return;
	}

	if (c == 'n') {
		next_music(st);
		return;
	}

	if (c == 'q') {
		st->mode = COMMAND;
		st->play_state = STOPPED;
		return;
	}

	if (c == 'l') {
		st->track_loop = (st->track_loop) ? 0 : 1;
		return;
	}
}

static void render_progress_bar(struct player_state* st, int width) {
	if (st->buf_len == 0) {
		return;
	}

	size_t current = st->cursor;
	size_t total = st->buf_len;

	float ratio = (float) current / (float) total;

	if (ratio > 1.0f) {
		ratio = 1.0f;
	}

	int filled = (int) (ratio * width);

	putchar('[');

	for (int i = 0; i < width; i++) {
		if (i < filled) {
			putchar('#');
		} else {
			putchar('-');
		}
	}

	putchar(']');
}

static void render_ui(struct player_state* st) {
	printf("\033[H\033[J");

	if (st->play_state == PLAYING) {
		struct track* t = get_current_music(st);
		printf("current track [%d/%d]: %s\n",
			st->current_track + 1, st->playlist.len, t->name);

		if (st->track_loop) {
			printf("looptrack: enabled\n");
		} else {
			printf("looptrack: disabled\n");
		}

		render_progress_bar(st, UI_WIDTH);
		printf("\n(space) play/pause  (n) next  (l) loop  (q) quit\n");
	}
}

void process_player_input(struct player_state* st) {
	char buf[32];

	ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n <= 0) {
		return;
	}

	for (ssize_t i = 0; i < n; i++) {
		process_key(st, buf[i]);
	}
}

void player_loop(struct player_state* st, volatile sig_atomic_t* should_exit) {
	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = 16 * 1000 * 1000
	};

	while (st->running && (st->mode == PLAYER)) {
		if (*should_exit) {
			st->running = 0;
		}

		process_player_input(st);
		int ret = play_wav_player_tick(st);

		if (ret == 0) {
			next_music(st);
		}

		render_ui(st);
		nanosleep(&ts, NULL);
	}
}

/*	--- CALLBACKS --- */

void print_wav(const char* path, const char* fullname, void* userdata) {
	(void) userdata;
	printf("%s\n", path);
}

