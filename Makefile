GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS := $(shell pkg-config --libs gtk+-3.0)
X11_LIBS := $(shell pkg-config --libs x11)
MATH_LIBS := -lm
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
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GLIB_CFLAGS) -o /tmp/esheep_test_pet_package tests/test_pet_package.c src/pet_package.c src/animations_data.c src/expression.c $(GLIB_LIBS) $(MATH_LIBS)
	/tmp/esheep_test_pet_package

test-runtime:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_runtime tests/test_runtime.c src/interpreter.c src/animations_data.c
	/tmp/esheep_test_runtime

test-expression:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GLIB_CFLAGS) -o /tmp/esheep_test_expression tests/test_expression.c src/expression.c $(GLIB_LIBS) $(MATH_LIBS)
	/tmp/esheep_test_expression

test-desktop:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_desktop tests/test_desktop.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/expression.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS) $(MATH_LIBS)
	/tmp/esheep_test_desktop

test-x11-refresh:
	command -v xvfb-run >/dev/null
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_x11_refresh tests/test_x11_refresh.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/expression.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS) $(MATH_LIBS)
	xvfb-run -a /tmp/esheep_test_x11_refresh

test-behavior:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_behavior tests/test_behavior.c src/actor.c src/animations_data.c src/interpreter.c src/expression.c src/renderer.c src/context.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS) $(MATH_LIBS)
	/tmp/esheep_test_behavior

test-multisheep:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc $(GTK_CFLAGS) -o /tmp/esheep_test_multisheep tests/test_multisheep.c src/actor.c src/interpreter.c src/animations_data.c src/expression.c src/context.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS) $(MATH_LIBS)
	/tmp/esheep_test_multisheep

test-performance: test-multisheep
	python3 tests/test_performance.py

test-gui: esheep
	command -v xvfb-run >/dev/null
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --character penguin --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --count 3 --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --package tools/esheep_animations.xml --sprite assets/sheep_spritesheet.png --no-window-landing
	xvfb-run -a env ESHEEP_AUTOQUIT_MS=250 ./esheep --config tests/test-config.ini
	xvfb-run -a env WAYLAND_DISPLAY=fake ESHEEP_AUTOQUIT_MS=250 ./esheep --x11-fallback --no-window-landing
	xvfb-run -a sh -c 'set -eu; ESHEEP_AUTOQUIT_MS=250 ./esheep --seed 101 --no-window-landing & first=$$!; ESHEEP_AUTOQUIT_MS=250 ./esheep --seed 202 --no-window-landing & second=$$!; wait "$$first"; wait "$$second"'

test-cli: esheep
	gcc -std=c11 -Wall -Wextra -Werror -o /tmp/esheep_test_cli tests/test_cli.c
	/tmp/esheep_test_cli

test-assets:
	python3 tests/test_spritesheet.py

test-visual-catalog:
	python3 tools/render_animation_catalog.py --output /tmp/esheep-animation-catalog.png
	test -s /tmp/esheep-animation-catalog.png
	rm -f /tmp/esheep-animation-catalog.png

gen-animations:
	python3 tools/gen_animations.py tools/esheep_animations.xml src

test-animation-sync:
	@set -eu; tmp=$$(mktemp -d /tmp/esheep-animation-sync.XXXXXX); trap 'rm -rf "$$tmp"' EXIT; \
	python3 tools/gen_animations.py tools/esheep_animations.xml "$$tmp"; \
	if ! diff -q src/animations_data.c "$$tmp/animations_data.c" || ! diff -q src/animations_data.h "$$tmp/animations_data.h"; then \
		echo "Generated animation data is out of sync. Run 'make gen-animations' to update." >&2; \
		exit 1; \
	fi

test-animation-data:
	python3 tests/test_animation_data.py

test-child-animations:
	python3 tests/test_child_animations.py

test-transition-parity: esheep
	python3 tests/test_transition_parity.py

test-child-scene-rendering: esheep
	command -v xvfb-run >/dev/null || command -v Xvfb >/dev/null
	python3 tests/test_child_scene_rendering.py

test-child-scene-rendering-strict: esheep
	command -v xvfb-run >/dev/null || command -v Xvfb >/dev/null
	ESHEEP_TEST_STRICT=1 python3 tests/test_child_scene_rendering.py

test-man:
	command -v groff >/dev/null
	groff -T utf8 -man packaging/esheep.1 >/dev/null

test-install:
	set -eu; stage=$$(mktemp -d /tmp/esheep-install-test.XXXXXX); trap 'rm -rf "$$stage"' EXIT; $(MAKE) install-autostart DESTDIR="$$stage" PREFIX=/usr; test -x "$$stage/usr/bin/esheep"; test -f "$$stage/usr/share/esheep/sheep_spritesheet.png"; test -f "$$stage/usr/share/esheep/penguin_ice_blue_spritesheet.png"; test -f "$$stage/usr/share/applications/esheep.desktop"; test -f "$$stage/usr/share/xdg/autostart/esheep.desktop"; test -f "$$stage/usr/share/man/man1/esheep.1"

test-context:
	gcc -std=c11 -Wall -Wextra -Werror -Isrc -o /tmp/esheep_test_context tests/test_context.c src/context.c
	/tmp/esheep_test_context

# Strict mode for CI: fails if Xvfb/ImageMagick are missing instead of skipping
test-strict: test-desktop test-x11-refresh test-behavior test-renderer test-actor test-pet-package test-interpreter test-runtime test-expression test-multisheep test-context test-animation-sync test-animation-data test-assets test-visual-catalog test-child-animations test-child-scene-rendering-strict test-man test-install test-gui test-cli

test: test-desktop test-x11-refresh test-behavior test-renderer test-actor test-pet-package test-interpreter test-runtime test-expression test-multisheep test-performance test-context test-animation-sync test-animation-data test-assets test-visual-catalog test-child-animations test-transition-parity test-child-scene-rendering test-man test-install test-gui test-cli

esheep: src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/expression.c src/renderer.c src/pet_package.c
	gcc -std=c11 -Wall -Wextra -Isrc $(GTK_CFLAGS) -o esheep src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/expression.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS) $(MATH_LIBS)

install: src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/expression.c src/renderer.c src/pet_package.c
	gcc -std=c11 -Wall -Wextra -O2 -Isrc $(GTK_CFLAGS) -DESHEEP_DATADIR=\"$(DATADIR)\" \
		-o /tmp/esheep-install-build src/main.c src/actor.c src/interpreter.c src/animations_data.c src/context.c src/expression.c src/renderer.c src/pet_package.c $(GTK_LIBS) $(X11_LIBS) $(MATH_LIBS)
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

.PHONY: all test test-desktop test-x11-refresh test-behavior test-renderer test-actor test-pet-package test-interpreter test-runtime test-expression test-multisheep test-performance test-context test-animation-sync test-animation-data test-assets test-visual-catalog test-child-animations test-transition-parity test-child-scene-rendering test-man test-install test-gui test-cli esheep install install-autostart uninstall uninstall-autostart clean gen-animations test-animation-sync
clean:
	rm -f esheep /tmp/esheep_test_desktop /tmp/esheep_test_x11_refresh /tmp/esheep_test_pet_package /tmp/esheep_test_renderer /tmp/esheep_test_actor /tmp/esheep_test_interpreter /tmp/esheep_test_runtime /tmp/esheep_test_expression /tmp/esheep_test_multisheep /tmp/esheep_test_behavior /tmp/esheep-install-build
