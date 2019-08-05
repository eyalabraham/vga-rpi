#####################################################################################
#
#  This make file is for compiling the Frame Buffer application
#  for Raspberry Pi. Make should be run on a Raspberry Pi.
#
#  Use:
#    clean      - clean environment
#    all        - build all outputs
#
#####################################################################################


#------------------------------------------------------------------------------------
# project directories
#------------------------------------------------------------------------------------
INCDIR = inc
SRCDIR = .
BINDIR = .

#------------------------------------------------------------------------------------
# build tool and options
#------------------------------------------------------------------------------------
CC = gcc
#OPT = -Wall -L/usr/local/lib -lbcm2835 -lxml2 -I $(INCDIR) -I/usr/include/libxml2
OPT = -Wall -L/usr/local/lib -lbcm2835 -I $(INCDIR)

#------------------------------------------------------------------------------------
# dependencies
#------------------------------------------------------------------------------------
DEPS = config.h util.h fb.h uart.h iv8x16u.h
OBJS = vga.o util.o fb.o uart.o

_DEPS = $(patsubst %,$(INCDIR)/%,$(DEPS))

#------------------------------------------------------------------------------------
# build all targets
#------------------------------------------------------------------------------------
%.o: %.c $(_DEPS)
	$(CC) -c -o $@ $< $(OPT)

all: vga

vga: $(OBJS)
	$(CC) $^ $(OPT) -o $@

#------------------------------------------------------------------------------------
# sync files and run remote 'make'
# requires ssh key setup to avoid using password authentication
#------------------------------------------------------------------------------------
sync:
	rsync -vrh /home/eyal/data/projects/pc-xt/vga-rpi/*  pi@10.0.0.16:/home/pi/vga
	rsh pi@10.0.0.16 "cd /home/pi/vga && make"
#	rsync -vrh /home/eyal/data/projects/pc-xt/vga-rpi/*  pi@10.0.0.13:/home/pi/Documents/vga-rpi
#	rsh pi@10.0.0.13 "cd /home/pi/Documents/vga-rpi && make"

rclean:
	rsh pi@10.0.0.16 "cd /home/pi/vga && make clean"
#	rsh pi@10.0.0.13 "cd /home/pi/Documents/vga-rpi && make clean"

#------------------------------------------------------------------------------------
# cleanup
#------------------------------------------------------------------------------------
.PHONY: clean

clean:
	rm -f vga
	rm -f *.o
	rm -f *.bak

