#include "fd_handle.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#define ECHO_FILE_NAME "echo.wav"

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
	uint8_t** data_buf, 
	size_t* buf_len
) 
{
	struct chunk_header chunk = {0};

	while (read(fd, &chunk, sizeof(struct chunk_header)) == sizeof(struct chunk_header)) {
		if (strncmp(chunk.id, "data", 4) == 0) {

			if (data) {
				memcpy(data->subchunk2_id, chunk.id, 4);
				data->subchunk2_size = chunk.size;
			}

			if (!data_buf) {
				return 0;
			}

			*data_buf = malloc(chunk.size);

			if (!*data_buf) {
				perror("malloc");
				return -1;
			}

			ssize_t n = read_bytes_from_file(fd, *data_buf, chunk.size);

			if (n != chunk.size) {
				free(*data_buf);
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

int read_wav_from_filename
(
	const char* filename,
	struct riff_header* riff,
	struct fmt_sub_chunk* fmt,
	struct data_sub_chunk* data,
	uint8_t** data_buf,
	size_t* buf_len,
	struct read_wav_result* read_result
)
{
	int file = open(filename, O_RDONLY | O_CREAT, 0644);
	ssize_t n;

	if (file < 0) {
		perror("open");
		return -1;
	}

	if (!riff) {
		if (lseek(file, sizeof(struct riff_header), SEEK_CUR) == -1) {
			perror("lseek riff_header");
			return -1;
		}
	} else {
		n = read_bytes_from_file(file, riff, sizeof(struct riff_header));

		if (n != sizeof(struct riff_header)) {
			close(file);
			return -1;
		}

		if (read_result) {
			read_result->riff = 1;
		}
	}

	if (!fmt) {
		if (lseek(file, sizeof(struct fmt_sub_chunk), SEEK_CUR) == -1) {
			perror("lseek fmt_sub_chunk");
			return -1;
		}
	} else {
		n = read_bytes_from_file(file, fmt, sizeof(struct fmt_sub_chunk));

		if (n != sizeof(struct fmt_sub_chunk)) {
			close(file);
			return -1;
		}

		if (read_result) {
			read_result->fmt = 1;
		}
	}

	read_data_chunk(file, data, data_buf, buf_len);

	if (data && read_result) {
		read_result->data = 1;
	}

	return file;
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