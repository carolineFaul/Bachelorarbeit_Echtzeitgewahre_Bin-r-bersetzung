CC=gcc
CFLAGS=-Wall -g -DAVR -DWCET
RM=rm

translator: translator.c 6502_instructions_avr.c 6502_instructions.h
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) translator
