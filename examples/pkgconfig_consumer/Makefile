# SPDX-License-Identifier: Apache-2.0
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: apta-pkgconfig-consumer

apta-pkgconfig-consumer: main.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libapta) $< -o $@ $(shell pkg-config --libs libapta)

clean:
	rm -f apta-pkgconfig-consumer
