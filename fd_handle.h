#ifndef FD_HANDLE_H
#define FD_HANDLE_H

#include "types.h"

ssize_t read_bytes_from_file(int fd, void* buf, size_t size);
ssize_t write_bytes_to_file(int fd, const void* buf, size_t size);

int read_data_chunk
(
	int fd, 
	struct data_sub_chunk* data, 
	uint8_t** data_buf, 
	size_t* buf_len
) ;

int read_wav_from_filename
(
	const char* filename,
	struct riff_header* riff,
	struct fmt_sub_chunk* fmt,
	struct data_sub_chunk* data,
	uint8_t** data_buf,
	size_t* buf_len,
	struct read_wav_result* read_result
);

int echo_wav
(
	const struct riff_header* riff,
	const struct fmt_sub_chunk* fmt,
	const struct data_sub_chunk* data,
	const uint8_t* data_buf,
	size_t buf_len
);

#endif