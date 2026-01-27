/*
simple wav player using ALSA (Advance Linux Sound Architecture)

HOW TO COMPILE:

gcc player.c -lasound

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

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <alsa/asoundlib.h>

#define ECHO_FILE_NAME "echo.wav"

struct riff_header {
	char chunk_id[4]; // "RIFF"
	uint32_t chunk_size; // file_size - 8 bytes
	char format[4]; // "WAVE"
}__attribute__((packed));

void print_riff_header(const struct riff_header* rhdr) {
	printf("	--- RIFF HEADER --- 	\n");
	printf("chunk_id: %.4s\n", rhdr->chunk_id);
	printf("chunk_size: %d\n", rhdr->chunk_size);
	printf("format: %.4s\n\n", rhdr->format);
}

struct fmt_sub_chunk {
	char subchunk1_id[4]; // "fmt "
	uint32_t subchunk1_size; // chunk size - 8 bytes (here, 16 bytes)
	uint16_t audio_format; // (1: PCM integer, 3: IEEE 754 float)
	uint16_t num_channels; // mono/stereo
	uint32_t sample_rate; // sample rate in hz
	uint32_t byte_rate; // bytes to read per sec (sample_rate * byte_align)
	uint16_t byte_align; // bytes per block (num_channels * bits_per_sample)
	uint16_t bits_per_sample; // bits per sample
}__attribute__((packed));

void print_fmt_sub_chunk(const struct fmt_sub_chunk* fmt) {
	printf("	--- FMT SUB CHUNK --- 	\n");
	printf("subchunk1_id: %.4s\n", fmt->subchunk1_id);
	printf("subchunk1_size: %d\n", fmt->subchunk1_size);
	printf("audio_format: %u\n", fmt->audio_format);
	printf("num_channels: %u\n", fmt->num_channels);
	printf("sample_rate: %d\n", fmt->sample_rate);
	printf("byte_rate: %d\n", fmt->byte_rate);
	printf("byte_align: %u\n", fmt->byte_align);
	printf("bits_per_sample: %u\n\n", fmt->bits_per_sample);
}

struct data_sub_chunk {
	char subchunk2_id[4]; // "data"
	uint32_t subchunk2_size; // sampled_data size
	// sampled_data
}__attribute__((packed));

void print_data_sub_chunk(const struct data_sub_chunk* data) {
	printf("	--- DATA SUB CHUNK --- 	\n");
	printf("subchunk2_id: %.4s\n", data->subchunk2_id);
	printf("subchunk2_size: %d\n\n", data->subchunk2_size);
}

struct chunk_header {
	char id[4];
	uint32_t size;
}__attribute__((packed));

ssize_t read_bytes_from_file(int fd, void* buf, size_t size) {
	ssize_t total_read = 0;;
	ssize_t n;

	while (total_read < size) {
		n = read(fd, buf + total_read, size - total_read);

		if (n < 0) {
			perror("read");
			break;
		}

		if (n == 0) {
			break;
		}

		total_read += n;
	}

	return total_read;
}

ssize_t write_bytes_to_file(int fd, const void* buf, size_t size) {
	ssize_t total_written = 0;
	ssize_t n;

	while (total_written < size) {
		ssize_t n = write(fd, 
			(const char*) buf +  total_written, size - total_written);

		if (n <= 0) {
			perror("write");
			break;
		}

		total_written += n;
	}

	return total_written;
}

int read_data_chunk
(
	int fd, 
	struct data_sub_chunk* data, 
	uint8_t** buf, 
	size_t* buf_len
) 
{
	struct chunk_header chunk = {0};

	while (read(fd, &chunk, sizeof(struct chunk_header)) == sizeof(struct chunk_header)) {
		if (strncmp(chunk.id, "data", 4) == 0) {
			memcpy(data->subchunk2_id, chunk.id, 4);
			data->subchunk2_size = chunk.size;

			*buf = malloc(chunk.size);

			if (!*buf) {
				perror("malloc");
				return -1;
			}

			ssize_t n = read_bytes_from_file(fd, *buf, chunk.size);

			if (n != chunk.size) {
				free(*buf);
				return -1;
			}

			*buf_len = (size_t) n;

			return 0;
		} else {
			if (lseek(fd, chunk.size, SEEK_CUR) == -1) {
				perror("lseek");
				return -1;
			}
		}
	}

	return -1;
}

int echo_wav
(
	const struct riff_header* riff,
	const struct fmt_sub_chunk* fmt,
	const struct data_sub_chunk* data,
	const uint8_t* data_buf,
	size_t buf_len
)
{
	int fd = open(ECHO_FILE_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	if (fd < 0) {
		perror("open");
		return -1;
	}

	if (write_bytes_to_file(fd, riff, sizeof(struct riff_header)) < sizeof(struct riff_header)) {
		perror("write riff");
		return -1;
	}

	if (write_bytes_to_file(fd, fmt, sizeof(struct fmt_sub_chunk)) < sizeof(struct fmt_sub_chunk)) {
		perror("write fmt");
		return -1;
	}

	if (write_bytes_to_file(fd, data, sizeof(struct data_sub_chunk)) < sizeof(struct data_sub_chunk)) {
		perror("write data");
		return -1;
	}

	if (write_bytes_to_file(fd, data_buf, buf_len) < buf_len) {
		perror("write data buf");
		return -1;
	}

	close(fd);
	return 0;
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

int main(int argc, char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s [WAV FILE]\n", argv[0]);
		return -1;
	}

	// --- READING .WAV ---

	char* file_name = argv[1];

	int file = open(file_name, O_RDONLY | O_CREAT, 0644);

	if (file < 0) {
		perror("open");
		return -1;
	}

	struct riff_header riff = {0};
	struct fmt_sub_chunk fmt = {0};
	struct data_sub_chunk data = {0};

	ssize_t n = read_bytes_from_file(file, &riff, sizeof(struct riff_header));

	if (n != sizeof(struct riff_header)) {
		close(file);
		return -1;
	}

	n = read_bytes_from_file(file, &fmt, sizeof(struct fmt_sub_chunk));

	if (n != sizeof(struct fmt_sub_chunk)) {
		close(file);
		return -1;
	}

	uint8_t* data_buf;
	size_t buf_len;
	int ret = read_data_chunk(file, &data, &data_buf, &buf_len);

	if (ret < 0) {
		close(file);
		return -1;
	}

	print_riff_header(&riff);
	print_fmt_sub_chunk(&fmt);
	print_data_sub_chunk(&data);

	// --- PLAYING .WAV ---

	int err = play_wav(data_buf, buf_len, &fmt);

	if (err < 0) {
		fprintf(stderr, "error opening device: %s\n", snd_strerror(err));
		free(data_buf);
		close(file);
		return -1;
	}

	close(file);
	return 0;
}

