fbkeyboard: fbkeyboard.c
	gcc -o fbkeyboard $(shell freetype-config --cflags) $(shell freetype-config --libs) $(CFLAGS) fbkeyboard.c

clean:
	rm fbkeyboard

