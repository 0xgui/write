CXX := g++
TARGET := build/write
PREFIX ?= /usr/local
CXXFLAGS := -std=c++20 -Os -Wall -Wextra -Wpedantic -DWRITE_DATA_DIR=\"$(PREFIX)/share/write\" $(shell pkg-config --cflags gtk4 fontconfig)
LDFLAGS := -s
LDLIBS := $(shell pkg-config --libs gtk4 fontconfig)
SOURCES := src/main.cpp src/ui.cpp src/storage.cpp src/positions.cpp src/settings.cpp src/essays.cpp src/fonts.cpp src/encoding.cpp
HEADERS := $(wildcard src/*.h)

.PHONY: all run test install clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS) $(LDLIBS)

build/storage_test: tests/storage_test.cpp src/storage.cpp src/storage.h
	@mkdir -p build
	$(CXX) -std=c++20 -O1 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags glib-2.0) tests/storage_test.cpp src/storage.cpp -o $@ $(shell pkg-config --libs glib-2.0)

build/settings_test: tests/settings_test.cpp src/settings.cpp src/settings.h
	@mkdir -p build
	$(CXX) -std=c++20 -O1 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags glib-2.0) tests/settings_test.cpp src/settings.cpp -o $@ $(shell pkg-config --libs glib-2.0)

build/essays_test: tests/essays_test.cpp src/essays.cpp src/essays.h
	@mkdir -p build
	$(CXX) -std=c++20 -O1 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags glib-2.0) tests/essays_test.cpp src/essays.cpp -o $@ $(shell pkg-config --libs glib-2.0)

build/shortcuts_test: tests/shortcuts_test.cpp src/shortcuts.h
	@mkdir -p build
	$(CXX) -std=c++20 -O1 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags glib-2.0) tests/shortcuts_test.cpp -o $@ $(shell pkg-config --libs glib-2.0)

build/encoding_test: tests/encoding_test.cpp src/encoding.cpp src/encoding.h
	@mkdir -p build
	$(CXX) -std=c++20 -O1 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags glib-2.0) tests/encoding_test.cpp src/encoding.cpp -o $@ $(shell pkg-config --libs glib-2.0)

test: build/storage_test build/settings_test build/essays_test build/shortcuts_test build/encoding_test
	./build/storage_test
	./build/settings_test
	./build/essays_test
	./build/shortcuts_test
	./build/encoding_test

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/write
	install -Dm644 com.silvags.Write.desktop $(DESTDIR)$(PREFIX)/share/applications/com.silvags.Write.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/applications/write.desktop
	install -Dm644 assets/com.silvags.Write.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/com.silvags.Write.svg
	install -Dm644 assets/Literata-VariableFont_opsz,wght.ttf $(DESTDIR)$(PREFIX)/share/write/Literata-VariableFont_opsz,wght.ttf
	install -Dm644 assets/OFL.txt $(DESTDIR)$(PREFIX)/share/write/OFL.txt
ifeq ($(DESTDIR),)
	-gtk-update-icon-cache -f -t $(PREFIX)/share/icons/hicolor 2>/dev/null || true
	-update-desktop-database $(PREFIX)/share/applications 2>/dev/null || true
endif

clean:
	rm -rf build
