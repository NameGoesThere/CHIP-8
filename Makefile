.PHONY: clean test

CFLAGS = -Wunused -lSDL2

# Use -debug=1 as a flag in your make command to enable debug mode.
ifdef debug
    CFLAGS += -DDEBUG
endif

chip8:
	cc $(CFLAGS) -o chip8 chip8.c

clean:
	rm -f chip8

test: chip8
	./chip8 "test.ch8"
