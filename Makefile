CC = gcc
CFLAGS = -I./src -g #-Wall -Wextra -Wstrict-prototypes

SRCS = loader nes clock ui/ui ui/video ui/audio ui/events cpu/cpu apu/apu memory/memory cartridge/cartridge
PKGCONFIG = glib-2.0 gio-2.0 glfw3 gl portaudio-2.0

BINDIR = bin
OBJDIR = obj
SRCDIR = src

CFLAGS += $(shell pkg-config --cflags $(PKGCONFIG))
LDFLAGS += $(shell pkg-config --libs $(PKGCONFIG))

.PHONY: all
all: loader

.PHONY: loader
loader: bin/loader
$(BINDIR)/loader: $(addprefix $(OBJDIR)/, $(addsuffix .o, $(SRCS)))
	@mkdir -p $(shell dirname $@)
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: run
run: loader
	./bin/loader

-include $(addprefix $(OBJDIR)/, $(addsuffix .d, $(SRCS)))

# Generic rule to build object files #
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(shell dirname $@)
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(BINDIR)
