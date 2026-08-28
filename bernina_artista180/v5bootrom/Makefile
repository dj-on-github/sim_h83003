# Build the v5 boot ROM: one permanent block and two interchangeable images.
#
#   make            -> v5boot.bin, the whole 32K device image
#
# The two images are the same code linked at different addresses. Only the
# generation differs, so which one a machine runs is decided at boot by
# stage-0 rather than at build time.
CROSS   = h8300-elf-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy

CFLAGS  = -mh -mint32 -Os -ffreestanding -fno-builtin -Wall -Wextra \
          -fomit-frame-pointer
LDBASE  = -mh -mint32 -nostdlib -nostartfiles -Wl,--gc-sections

IMAGE_A_BASE = 0x000800
IMAGE_A_LEN  = 0x3800
IMAGE_B_BASE = 0x004000
IMAGE_B_LEN  = 0x4000

# What makes the update visible from the host: the image an update installs
# answers 'I' with a different banner, so "did it take" is one command away.
NEW_BANNER = -DBIOS_BANNER='"BiosVersion: 1.11\r"' -DBIOS_DATE='"Aug 26\r"'

all: v5boot.bin updater.bin imageN.bin mergebootrom

stage0.o: stage0.S ; $(CC) $(CFLAGS) -c $< -o $@
stage0.elf: stage0.o stage0.ld
	$(CC) $(LDBASE) -T stage0.ld $< -o $@
stage0.bin: stage0.elf ; $(OBJCOPY) -O binary $< $@

# One image. $(1) is the letter, $(2) the base, $(3) the length, $(4) the
# generation, $(5) any extra flags for boot.c.
define IMAGE_RULES
image$(1).ld: image.ld.in
	$$(CC) -E -P -x c -DIMAGE_BASE=$(2) -DIMAGE_LEN=$(3) $$< -o $$@
image$(1)_img.o: image.S Makefile
	$$(CC) $$(CFLAGS) -DIMAGE_GEN=$(4) -c $$< -o $$@
image$(1)_boot.o: boot.c h8_3003.h Makefile
	$$(CC) $$(CFLAGS) $(5) -c boot.c -o $$@
image$(1).elf: image$(1)_img.o image$(1)_boot.o image$(1).ld
	$$(CC) $$(LDBASE) -T image$(1).ld image$(1)_img.o image$(1)_boot.o -o $$@
image$(1).bin: image$(1).elf
	$$(OBJCOPY) -O binary $$< $$@
	python3 mkimage.py $$@
endef

$(eval $(call IMAGE_RULES,A,$(IMAGE_A_BASE),$(IMAGE_A_LEN),1,))
$(eval $(call IMAGE_RULES,B,$(IMAGE_B_BASE),$(IMAGE_B_LEN),2,))

# The payload an update installs: the same code again, linked for slot A and
# stamped generation 3, so stage-0 prefers it to the generation-2 image the
# machine is running. Nothing has to record which slot is live.
$(eval $(call IMAGE_RULES,N,$(IMAGE_A_BASE),$(IMAGE_A_LEN),3,$(NEW_BANNER)))

# Lay the pieces out in a 32K device image: the permanent block at 0, image A
# at H'000600, image B at H'004000. Gaps are left erased.
v5boot.bin: stage0.bin imageA.bin imageB.bin assemble.py
	python3 assemble.py $@

updater.o: updater.c h8_3003.h
	$(CC) $(CFLAGS) -c $< -o $@
updater.elf: updater.o updater.ld
	$(CC) $(LDBASE) -T updater.ld $< -o $@
updater.bin: updater.elf ; $(OBJCOPY) -O binary $< $@

HOSTCC     = cc
HOSTCFLAGS = -std=c99 -Wall -Wextra -pedantic -O2
mergebootrom: mergebootrom.c ; $(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean: ; rm -f *.o *.elf *.bin image?.ld mergebootrom updater.elf
.PHONY: all clean
