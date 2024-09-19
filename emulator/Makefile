CC=gcc
CFLAGS=-Wall -g -DDECIMALMODE
RM=rm

emulator: emulator_6502.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) emulator
