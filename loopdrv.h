#ifndef _LOOPDRV_H_
#define _LOOPDRV_H_

#include <stdint.h>

//****************************************************************************
// Human68k structure definitions
//****************************************************************************

struct dos_req_header {
  uint8_t magic;       // +0x00.b  Constant (26)
  uint8_t unit;        // +0x01.b  Unit number
  uint8_t command;     // +0x02.b  Command code
  uint8_t errl;        // +0x03.b  Error code low
  uint8_t errh;        // +0x04.b  Error code high
  uint8_t reserved[8]; // +0x05 .. +0x0c  not used
  uint8_t attr;        // +0x0d.b  Attribute / Seek mode
  void *addr;          // +0x0e.l  Buffer address
  uint32_t status;     // +0x12.l  Bytes / Buffer / Result status
  void *fcb;           // +0x16.l  FCB
} __attribute__((packed, aligned(2)));

struct dos_bpb {
  uint16_t sectbytes;  // +0x00.b  Bytes per sector
  uint8_t sectclust;   // +0x02.b  Sectors per cluster
  uint8_t fatnum;      // +0x03.b  Number of FATs
  uint16_t resvsects;  // +0x04.w  Reserved sectors
  uint16_t rootent;    // +0x06.w  Root directory entries
  uint16_t sects;      // +0x08.w  Total sectors
  uint8_t mediabyte;   // +0x0a.b  Media byte
  uint8_t fatsects;    // +0x0b.b  Sectors per FAT
  uint32_t sectslong;  // +0x0c.l  Total sectors (long)
};

//****************************************************************************
// Private structure definitions
//****************************************************************************

#define MAX_DRIVES      8
#define LOOPDRV_VERSION 2

struct lodrive {
  int status;                   // Disk status 
                                // (0:not mounted, 1:changed, 2:mounted, -1:error)
  int drive;                    // Drive number
  int fd;                       // File descriptor
  int readonly;                 // Read only flag
  int offset;                   // Offset from the start of the file
  int interleave;               // Interleave data size (for D88 format)
  int over2gb;                  // Over 2GB flag
  struct dos_bpb bpb;           // BIOS Parameter Block
  char filename[256];           // File name
};

struct loopdrv_param {
  int loopdrv_ver;
  int num_drives;
  int default_readonly;
  struct lodrive drive[MAX_DRIVES];
};

#endif /* _LOOPDRV_H_ */
