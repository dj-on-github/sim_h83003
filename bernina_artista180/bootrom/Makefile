# Build the boot ROM. Produces a raw binary that can be spliced into a full
# memory image and run in the simulator.
CROSS   = h8300-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size

# -mh selects the H8/300H; without it the compiler emits base H8/300 code and
# the link fails on an architecture mismatch. -mint32 makes int 32 bits, so
# that promotions behave the way the C reads.
CFLAGS  = -mh -mint32 -Os -ffreestanding -fno-builtin -Wall -Wextra \
          -fomit-frame-pointer
LDFLAGS = -mh -mint32 -nostdlib -nostartfiles -T boot.ld -Wl,--gc-sections

OBJS = vectors.o boot.o

all: bootrom.bin mergebootrom

%.o: %.S ; $(CC) $(CFLAGS) -c $< -o $@
%.o: %.c h8_3003.h ; $(CC) $(CFLAGS) -c $< -o $@

bootrom.elf: $(OBJS) boot.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

bootrom.bin: bootrom.elf
	$(OBJCOPY) -O binary $< $@
	@echo "size: $$(wc -c < $@) bytes"

# mergebootrom is a host tool, not target code, so it uses the system
# compiler rather than the cross one.
HOSTCC      = cc
HOSTCFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2

mergebootrom: mergebootrom.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean: ; rm -f *.o bootrom.elf bootrom.bin mergebootrom
.PHONY: all clean
