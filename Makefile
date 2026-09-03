GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)
X11_LIBS := $(shell pkg-config --libs x11)
GLIB_CFLAGS := $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS := $(shell pkg-config --libs glib-2.0)

PREFIX ?= /usr/local
DATADIR := $(PREFIX)/share/esheep
BINDIR := $(PREFIX)/bin
APPDIR := $(PREFIX)/share/applications
AUTOSTARTDIR := $(PREFIX)/share/xdg/autostart
MANDIR := $(PREFIX)/share/man/man1

all: esheep

test-interpreter:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_interpreter tests/test_interpreter.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_interpreter

test-renderer:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_renderer tests/test_renderer.c src/renderer.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_renderer

test-actor:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_actor tests/test_actor.c src/actor.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_actor

test-pet-package:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GLIB_CFLAGS) -o /tmp/esheep_test_pet_package tests/test_pet_package.c src/pet_package.c src/animations_data.c $(GLIB_LIBS)
	/tmp/esheep_test_pet_package

test-runtime:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_runtime tests/test_runtime.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_runtime

test-desktop:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_desktop tests/test_desktop.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS)
	/tmp/esheep_test_desktop

test-x11-refresh:
	command -v xvfb-run >/dev/null
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_x11_refresh tests/test_x11_refresh.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS)
	xvfb-run -a /tmp/esheep_test_x11_refresh

test-behavior:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_behavior tests/test_behavior.c src/actor.c src/animations_data.c src/interpreter.c src/renderer.c src/context.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS)
	/tmp/esheep_test_behavior

test-multisheep:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_multisheep tests/test_multisheep.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS)
	/tmp/esheep_test_multisheep

test-gui: esheep
	command -v xvfb-run >/dev/null
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --character penguin --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --count 3 --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --package tools/esheep_animations.xml --sprite assets/sheep_spritesheet.png --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --config tests/test-config.ini
	xvfb-run -a env WAYLAND_DISPLAY=fake ESHEEP_AUTOQUIT_MS=250 ./esheep --x11-fallback --no-window-landing

test-cli: esheep
	gcc -std=c11 -Wall -Wextra -Werror -o /tmp/esheep_test_cli tests/test_cli.c
	/tmp/esheep_test_cli

test-assets:
	python3 tests/test_spritesheet.py

test-animation-data:
	python3 tests/test_animation_data.py

test-child-animations:
	python3 tests/test_child_animations.py

test-context:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_context tests/test_context.c src/context.c
	/tmp/esheep_test_context

test: test-desktop test-x11-refresh test-behavior test-renderer test-actor test-pet-package test-interpreter test-runtime test-multisheep test-context test-animation-data test-assets test-child-animations test-gui test-cli

esheep: src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c
	gcc -std=c11 -Wall -Wextra -Isrc $(GTK_CFLAGS) -o esheep src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS)

install: src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c
	gcc -std=c11 -Wall -Wextra -O2 -Isrc $(GTK_CFLAGS) -DESHEEP_DATADIR=\"$(DATADIR)\" \
		-o /tmp/esheep-install-build src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS)
	install -Dm755 /tmp/esheep-install-build $(DESTDIR)$(BINDIR)/esheep
	install -Dm644 assets/sheep_spritesheet.png $(DESTDIR)$(DATADIR)/sheep_spritesheet.png
	install -Dm644 assets/penguin_ice_blue_spritesheet.png $(DESTDIR)$(DATADIR)/penguin_ice_blue_spritesheet.png
	install -Dm644 packaging/esheep.desktop $(DESTDIR)$(APPDIR)/esheep.desktop
	install -Dm644 packaging/esheep.1 $(DESTDIR)$(MANDIR)/esheep.1
	rm -f /tmp/esheep-install-build

install-autostart: install
	install -Dm644 packaging/esheep-autostart.desktop $(DESTDIR)$(AUTOSTARTDIR)/esheep.desktop

uninstall-autostart:
	rm -f $(DESTDIR)$(AUTOSTARTDIR)/esheep.desktop

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/esheep
	rm -f $(DESTDIR)$(DATADIR)/sheep_spritesheet.png
	rm -f $(DESTDIR)$(DATADIR)/penguin_ice_blue_spritesheet.png
	rmdir $(DESTDIR)$(DATADIR) 2>/dev/null || true
	rm -f $(DESTDIR)$(APPDIR)/esheep.desktop
	rm -f $(DESTDIR)$(MANDIR)/esheep.1

.PHONY: all test test-desktop test-x11-refresh test-behavior test-renderer test-actor test-pet-package test-interpreter test-runtime test-multisheep test-context test-animation-data test-assets test-child-animations test-gui test-cli esheep install install-autostart uninstall uninstall-autostart clean
clean:
	rm -f esheep /tmp/esheep_test_desktop /tmp/esheep_test_x11_refresh /tmp/esheep_test_pet_package /tmp/esheep_test_renderer /tmp/esheep_test_actor /tmp/esheep_test_interpreter /tmp/esheep_test_runtime /tmp/esheep_test_multisheep /tmp/esheep_test_behavior /tmp/esheep-install-build
