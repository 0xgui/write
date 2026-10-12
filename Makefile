CXX := g++
TARGET := build/write
PREFIX ?= /usr/local
CXXFLAGS := -std=c++20 -Os -Wall -Wextra -Wpedantic -DWRITE_DATA_DIR=\"$(PREFIX)/share/write\" $(shell pkg-config --cflags gtk4 fontconfig)
LDFLAGS := -s
LDLIBS := $(shell pkg-config --libs gtk4 fontconfig)
SOURCES := src/main.cpp src/ui.cpp src/storage.cpp src/positions.cpp src/fonts.cpp
HEADERS := $(wildcard src/*.h)

.PHONY: all run install clean

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/write
	install -Dm644 write.desktop $(DESTDIR)$(PREFIX)/share/applications/write.desktop
	install -Dm644 assets/com.silvags.Write.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/com.silvags.Write.svg
	install -Dm644 assets/Literata-VariableFont_opsz,wght.ttf $(DESTDIR)$(PREFIX)/share/write/Literata-VariableFont_opsz,wght.ttf
	install -Dm644 assets/OFL.txt $(DESTDIR)$(PREFIX)/share/write/OFL.txt

clean:
	rm -rf build
