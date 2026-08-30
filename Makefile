GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)
X11_LIBS := $(shell pkg-config --libs x11)

PREFIX ?= /usr/local
DATADIR := $(PREFIX)/share/esheep
BINDIR := $(PREFIX)/bin
APPDIR := $(PREFIX)/share/applications
MANDIR := $(PREFIX)/share/man/man1

all: esheep

test-interpreter:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_interpreter tests/test_interpreter.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_interpreter

test-runtime:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_runtime tests/test_runtime.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_runtime

test-gui: esheep
	command -v xvfb-run >/dev/null
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --character penguin --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --count 3 --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --config tests/test-config.ini
	xvfb-run -a env WAYLAND_DISPLAY=fake ESHEEP_AUTOQUIT_MS=250 ./esheep --x11-fallback --no-window-landing

test-assets:
	python3 tests/test_spritesheet.py

test-animation-data:
	python3 tests/test_animation_data.py

test-child-animations:
	python3 tests/test_child_animations.py

test: test-interpreter test-runtime test-animation-data test-assets test-child-animations test-gui

esheep: src/main.c src/interpreter.c src/animations_data.c
	gcc -std=c11 -Wall -Wextra -Isrc $(GTK_CFLAGS) -o esheep src/main.c src/interpreter.c src/animations_data.c $(GTK_LIBS) $(X11_LIBS)

install: src/main.c src/interpreter.c src/animations_data.c
	gcc -std=c11 -Wall -Wextra -O2 -Isrc $(GTK_CFLAGS) -DESHEEP_DATADIR=\"$(DATADIR)\" \
		-o /tmp/esheep-install-build src/main.c src/interpreter.c src/animations_data.c $(GTK_LIBS) $(X11_LIBS)
	install -Dm755 /tmp/esheep-install-build $(DESTDIR)$(BINDIR)/esheep
	install -Dm644 assets/sheep_spritesheet.png $(DESTDIR)$(DATADIR)/sheep_spritesheet.png
	install -Dm644 assets/penguin_ice_blue_spritesheet.png $(DESTDIR)$(DATADIR)/penguin_ice_blue_spritesheet.png
	install -Dm644 packaging/esheep.desktop $(DESTDIR)$(APPDIR)/esheep.desktop
	install -Dm644 packaging/esheep.1 $(DESTDIR)$(MANDIR)/esheep.1
	rm -f /tmp/esheep-install-build

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/esheep
	rm -f $(DESTDIR)$(DATADIR)/sheep_spritesheet.png
	rm -f $(DESTDIR)$(DATADIR)/penguin_ice_blue_spritesheet.png
	rmdir $(DESTDIR)$(DATADIR) 2>/dev/null || true
	rm -f $(DESTDIR)$(APPDIR)/esheep.desktop
	rm -f $(DESTDIR)$(MANDIR)/esheep.1

.PHONY: all test test-interpreter test-runtime test-animation-data test-assets test-gui esheep install uninstall clean
clean:
	rm -f esheep /tmp/esheep_test_interpreter /tmp/esheep_test_runtime /tmp/esheep-install-build
