CC=gcc
CFLAGS=-Wall -g -DC -DWCET -DOPTIMIZATION
RM=rm

translator: translator.c 6502_instructions_c.c 6502_instructions.h
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) translator
