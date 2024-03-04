#
# Copyright (c) 2024 Yuichi Nakamura (@yunkya2)
#
# The MIT License (MIT)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

CROSS = m68k-xelf-
CC = $(CROSS)gcc
AS = $(CROSS)gcc
LD = $(CROSS)gcc

GIT_REPO_VERSION=$(shell git describe --tags --always)

CFLAGS = -g -m68000 -I. -Os -DGIT_REPO_VERSION=\"$(GIT_REPO_VERSION)\"
CFLAGS += -finput-charset=utf-8 -fexec-charset=cp932
ASFLAGS = -m68000 -I.

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG
endif

all: LOOPDRV.SYS losetup.x

LOOPDRV.SYS: head.o diskiofix.o loopdrv.o
	$(LD) -o $@ $^ -nostartfiles -s

losetup.x: losetup.o
	$(LD) -o $@ $^ -s

loopdrv.o: loopdrv.h
losetup.o: loopdrv.h

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.S
	$(AS) $(ASFLAGS) -c $<

clean:
	-rm -f *.o *.SYS *.elf* *.x
	-rm -rf build

RELFILE := loopdrv-$(GIT_REPO_VERSION)

release: all
	rm -rf build && mkdir build
	iconv -f utf-8 -t cp932 README.md | sed 's/$$/\r/' > build/README.txt
	cp LOOPDRV.SYS build
	cp losetup.x build
	(cd build; zip -r ../$(RELFILE).zip *)

.PHONY: all clean release
