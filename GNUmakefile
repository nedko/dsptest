CFLAGS := -Wall
CFLAGS += -Wextra
CFLAGS += -Werror
CFLAGS += -O0

all: build run

build:
	gcc $(CFLAGS) -lpthread -lm dsptest.c -o dsptest

run:
	./dsptest
