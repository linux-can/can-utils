PREFIX := /usr/local


default: testj1939
all: default $(patsubst %.md, %.html, $(wildcard *.md))

%.html: %.md page.theme
	theme -f -o $@ $< -p "$*"

CFLAGS	+= -Wall -g3 -O0
CPPFLAGS += -Iinclude/uapi

install:
	install -D -m 755 testj1939 ${DESTDIR}${PREFIX}/bin/testj1939

clean:
	rm -f testj1939 $(wildcard *.html)
