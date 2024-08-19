SRCS = $(wildcard src/*.c)
TARGET = HexCurse
LIBS = -lncurses
CFLAGS = -Wall -Wextra -pedantic -g -std=c11 -D_POSIX_SOURCE

OBJS = $(SRCS:src/%.c=bin/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	gcc $(CFLAGS) -o bin/$(TARGET) $(OBJS) $(LIBS)

bin/%.o: src/%.c
	mkdir -p bin
	gcc $(CFLAGS) -c $< -o $@

run:
	./bin/$(TARGET)

clean:
	rm -f $(OBJS) bin/$(TARGET)

.PHONY: all clean
