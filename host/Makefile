# The host side: software that runs on a PC and talks to the machine.
#
# artista180_burn_application puts an application image into the machine's
# flash over the serial link, through the download protocol in its boot ROM.
# See the comment at the top of the source for the sequence and for how the
# machine has to be caught.
#
# Tested against the simulator rather than only against hardware:
#
#   python3 ../tool/burn_test.py ../bernina_artista180/Bernina180_20260816.bin \
#           ../bernina_artista180/application/app.bin
#
# which makes a pty pair, points this at the slave -- a serial port as far as
# open() and termios are concerned -- and relays the master to
# tool/serial_stdio.dart running the machine.

CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2

all: artista180_burn_application

artista180_burn_application: artista180_burn_application.c
	$(CC) $(CFLAGS) $< -o $@

clean: ; rm -f artista180_burn_application
.PHONY: all clean
