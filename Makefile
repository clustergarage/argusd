TARGET = fim_inotify

bin/$(TARGET): include/$(TARGET).o include/$(TARGET).a
	gcc $^ -o $@

include/$(TARGET).a: include/$(TARGET).o
	ar rcs $@ $^

include/$(TARGET).o: src/$(TARGET).c
	gcc -c -o $@ $<

clean:
	rm -f include/${TARGET}.{o,a} bin/$(TARGET)
