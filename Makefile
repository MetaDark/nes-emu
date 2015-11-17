CC = gcc
CFLAGS = -I./src -g -Wall -Wextra -Wstrict-prototypes

SRCS = main nes clock
SRCS += cpu/cpu memory/memory cartridge/cartridge mapper/mapper
SRCS += apu/apu apu/length-table apu/pulse apu/triangle apu/noise apu/dmc
SRCS += ui/ui ui/video ui/audio ui/events

PKGCONFIG = glib-2.0 gio-2.0 gmodule-2.0 glfw3 gl portaudio-2.0

CFLAGS += $(shell pkg-config --cflags $(PKGCONFIG))
LDFLAGS += $(shell pkg-config --libs $(PKGCONFIG))

.PHONY: all
all: main mappers

# Main
.PHONY: main
main: bin/main
bin/main: $(addprefix obj/, $(addsuffix .o, $(SRCS)))
	@mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) $^ -o $@

# Mappers
.PHONY: mappers
mappers: mapper/0.so

mapper/%.so: obj/mapper/mapper-%.o
	@mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -shared $^ -o $@

obj/mapper/mapper-%.o: src/mapper/mapper-%.c
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -fPIC -MMD -c $< -o $@

# Generic rule to build object files #
obj/%.o: src/%.c
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

# Include dependencies generated from 'gcc -MMD'
-include $(addprefix obj/, $(addsuffix .d, $(SRCS)))

.PHONY: run
run: all
	./bin/main

.PHONY: clean
clean:
	rm -rf obj bin mapper
