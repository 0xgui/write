CXX := g++
TARGET := build/write
PREFIX ?= /usr/local
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -DWRITE_DATA_DIR=\"$(PREFIX)/share/write\" $(shell pkg-config --cflags gtk4 fontconfig)
LDLIBS := $(shell pkg-config --libs gtk4 fontconfig)

.PHONY: all run install clean

all: $(TARGET)

$(TARGET): src/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/write
	install -Dm644 write.desktop $(DESTDIR)$(PREFIX)/share/applications/write.desktop
	install -Dm644 assets/Literata-VariableFont_opsz,wght.ttf $(DESTDIR)$(PREFIX)/share/write/Literata-VariableFont_opsz,wght.ttf
	install -Dm644 assets/OFL.txt $(DESTDIR)$(PREFIX)/share/write/OFL.txt

clean:
	rm -rf build
