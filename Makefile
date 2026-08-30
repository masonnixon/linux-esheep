GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)

test-interpreter:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_interpreter tests/test_interpreter.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_interpreter

esheep: src/main.c src/interpreter.c src/animations_data.c
	gcc -std=c11 -Wall -Wextra -Isrc $(GTK_CFLAGS) -o esheep src/main.c src/interpreter.c src/animations_data.c $(GTK_LIBS)

.PHONY: test-interpreter esheep clean
clean:
	rm -f esheep /tmp/esheep_test_interpreter
