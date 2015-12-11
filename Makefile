
default: testj1939
all: default $(patsubst %.md, %.html, $(wildcard *.md))

%.html: %.md page.theme
	theme -f -o $@ $< -p "$*"

CFLAGS	= -Wall -g3 -O0

clean:
	rm -f testj1939 j1939.html
