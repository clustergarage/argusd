SOURCELIB = lib_fiminotify
SOURCECLI = fiminotify_cli
TARGET = libfiminotify
BINARY = fim_inotify

bin/$(BINARY): src/$(SOURCECLI).c lib/$(TARGET).o lib/$(TARGET).a
	gcc -o $@ $^ 

lib/$(TARGET).a: lib/$(TARGET).o
	ar rcs $@ $^

lib/$(TARGET).o: src/$(SOURCELIB).c src/$(SOURCELIB).h
	gcc -c -o $@ $<

clean:
	rm -f lib/$(TARGET).* bin/$(BINARY)
