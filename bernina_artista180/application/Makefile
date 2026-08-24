# Build the application. Produces a raw binary that mergeapp splices into a
# full memory image, leaving everything above it as it was.
CROSS   = h8300-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size

CFLAGS  = -mh -mint32 -Os -ffreestanding -fno-builtin -Wall -Wextra \
          -fomit-frame-pointer
# -lgcc supplies the arithmetic the H8 has no instruction for: 32-bit
# divide and multiply, and the floating point the application uses in its
# coordinate maths. -nostdlib drops it along with the C library, so it is
# named again explicitly.
LDFLAGS = -mh -mint32 -nostdlib -nostartfiles -T app.ld -Wl,--gc-sections
LDLIBS  = -lgcc

OBJS = vectors.o app.o

all: app.bin app.sym mergeapp

%.o: %.S ; $(CC) $(CFLAGS) -c $< -o $@
%.o: %.c ../bootrom/h8_3003.h ; $(CC) $(CFLAGS) -c $< -o $@

app.elf: $(OBJS) app.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)
	$(SIZE) $@

app.bin: app.elf
	$(OBJCOPY) -O binary $< $@
	@echo "size: $$(wc -c < $@) bytes"

# The symbol file the comparison harness resolves names through, so that
# cases keep working when addresses move -- which they do on every build.
app.sym: app.elf
	$(CROSS)nm -n $< > $@

# Host tool, system compiler.
HOSTCC     = cc
HOSTCFLAGS = -std=c99 -Wall -Wextra -pedantic -O2

mergeapp: mergeapp.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean: ; rm -f *.o app.elf app.bin app.sym mergeapp
.PHONY: all clean
