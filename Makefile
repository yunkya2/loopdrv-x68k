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
OBJCOPY = $(CROSS)objcopy
LINK = $(CROSS)ld

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

OBJS = head.o loopdrv.o loopdrv.o diskiopatch.o

all: losetup.x

losetup.x: head.o tsrhead.o losetup.o diskiopatch.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

TSRLIBS = $(shell $(CC) -print-libgcc-file-name)
TSRLIBS += $(shell $(CC) -print-file-name=libx68kdos.a)
TSRLIBS += $(shell $(CC) -print-file-name=libx68kiocs.a)

tsr.o: loopdrv.o
	$(LINK) -r -o $@ $^ $(TSRLIBS)

tsrhead.o: tsr.o
	$(OBJCOPY) \
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
	-rm -f *.o *.x *.elf *.map *.d

RELFILE := loopdrv-$(GIT_REPO_VERSION)

release: clean all
	./md2txtconv.py README.md
	zip -r $(RELFILE).zip README.txt losetup.x

-include $(DEPS)

.PHONY: all clean release
