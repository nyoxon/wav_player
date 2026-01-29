CC := gcc
CFLAGS :=
TARGET = player
LDLIBS = -lasound

SRCS = player.c cli_interface.c sound_engine.c types.c fd_handle.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET)