
OUTPUT	= j1939.html testj1939
default: $(OUTPUT)

%.html: %.page page.theme
	theme -f -o $@ $< -p "$*"

CFLAGS	= -Wall -g3 -O0

clean:
	rm -f $(wildcard *.o) $(OUTPUT)
