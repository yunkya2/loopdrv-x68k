#
# Copyright (c) 2024,2026 Yuichi Nakamura (@yunkya2)
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

CFLAGS = -Os -g $(INC) $(DEFS) -MMD -MP
CFLAGS += -DGIT_REPO_VERSION=\"$(GIT_REPO_VERSION)\"
ASFLAGS = -I. -MMD -MP
LDFLAGS = -s
LDFLAGS += -Wl,-Map,$(@:.x=.map) -specs=nano.specs

INC += -I.
DEFS +=

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG
endif

all: losetup.x

losetup.x: head.o tsr.o losetup.o diskiopatch.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

OBJS = head.o diskiopatch.o loopdrv.o losetup.o

TSRLIBS = $(shell $(CC) -print-libgcc-file-name)
TSRLIBS += $(shell $(CC) -print-file-name=libx68kdos.a)
TSRLIBS += $(shell $(CC) -print-file-name=libx68kiocs.a)

tsr0.o: loopdrv.o
	$(CROSS)ld -r -o $@ $^ $(TSRLIBS)

tsr.o: tsr0.o
	$(CROSS)objcopy \
		--rename-section .text=.header \
		--rename-section .data=.header,alloc,readonly,code \
		--rename-section .bss=.header,alloc,readonly,code \
	$< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.S
	$(AS) $(ASFLAGS) -c $<

DEPS = $(OBJS:.o=.d)

clean:
	-rm -f *.o *.SYS *.elf* *.x *.elf* *.map *.d
	-rm -rf build

RELFILE := loopdrv-$(GIT_REPO_VERSION)

release: all
	rm -rf build && mkdir build
	iconv -f utf-8 -t cp932 README.md | sed 's/$$/\r/' > build/README.txt
	cp LOOPDRV.SYS build
	cp losetup.x build
	(cd build; zip -r ../$(RELFILE).zip *)

-include $(DEPS)

.PHONY: all clean release
