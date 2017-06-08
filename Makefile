fbkeyboard: fbkeyboard.c
	gcc -o fbkeyboard $(shell freetype-config --cflags) $(shell freetype-config --libs) $(CFLAGS) $(LDFLAGS) fbkeyboard.c

clean:
	rm -f fbkeyboard

