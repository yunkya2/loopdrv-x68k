/*
 * Copyright (c) 2024,2026 Yuichi Nakamura (@yunkya2)
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _LOOPDRV_H_
#define _LOOPDRV_H_

#include <stdint.h>

//#define DEBUG

#define CONFIG_DEVNAME "\x02LOOPDRV"
#define LOOPDRV_VERSION 2

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
} __attribute__((packed, aligned(2)));

struct dos_devheader {
    struct dos_devheader *next;
    uint16_t    attr;
    void        *strategy;
    void        *interrupt;
    char        name[8];

    void        *param;
} __attribute__((packed, aligned(2)));

// Drive Parameter Block
struct dos_dpb {
    int8_t drive;             // +0x00.b ドライブ番号
    int8_t unit;              // +0x01.b ユニット番号
    struct dos_devheader *devheader;  // +0x02.l デバイスヘッダへのポインタ
    struct dos_dpb *next;             // +0x06.l 次のDPBへのポインタ
    uint16_t sectbytes;       // +0x0a.w 1セクタあたりのバイト数

    uint8_t sectclust;        // +0x0c.b 1クラスタあたりのセクタ数-1
    uint8_t csshift;          // +0x0d.b クラスタ→セクタのシフト数
    uint16_t fatsect;         // +0x0e.w FATの先頭セクタ番号
    uint8_t fatnum;           // +0x10.b FATの数
    uint8_t fatsects;         // +0x11.b 1個のFATあたりのセクタ数
    uint16_t rootent;         // +0x12.w ルートディレクトリエントリ数
    uint16_t datasect;        // +0x14.w データ領域の先頭セクタ番号
    uint16_t totalclu;        // +0x16.w 総クラスタ数+1
    uint16_t rootsect;        // +0x18.w ルートディレクトリの先頭セクタ番号
    uint8_t mediabyte;        // +0x1a.b メディアバイト
    uint8_t sbshift;          // +0x1b.b セクタ→バイトのシフト数
    uint16_t fatfindpos;      // +0x1c.w FAT検索開始位置

    uint32_t schdir_firstfat; // +0x1e.l
    uint16_t schdir_clusect;  // +0x22.w
    uint32_t schdir_nextsect; // +0x24.l
    uint16_t schdir_remsect;  // +0x28.w

    uint32_t schfil_firstfat; // +0x2a.l
    uint16_t schfil_clusect;  // +0x2e.w
    uint32_t schfil_nextsect; // +0x30.l
    uint16_t schfil_remsect;  // +0x34.w
    uint16_t schfil_offset;   // +0x36.w
} __attribute__((packed,aligned(2)));

// Current Directory Table
struct dos_curdir {
    uint8_t drive;            // +0x00.b 物理ドライブ名
    uint8_t coron;            // +0x01.b コロン文字 (':')
    uint8_t path[62];         // +0x02.b カレントディレクトリのパス (デリミタは'\t')
    uint32_t reserved1;       // +0x40.l
    uint8_t reserved2;        // +0x44.b
    uint8_t type;             // +0x45.b ドライブ種別
    struct dos_dpb *dpb;      // +0x46.l DPBへのポインタ
    uint16_t curfat;          // +0x4a.w
    uint16_t pathlen;         // +0x4c.w
} __attribute__((packed,aligned(2)));

//****************************************************************************
// Private structure definitions
//****************************************************************************

#define MAX_DRIVES      8
#define LOOPDRV_VERSION 2

struct lodrive {
  int status;                   // Disk status 
                                // (0:not mounted, 1:changed, 2:mounted, -1:error)
  int fd;                       // File descriptor
  int readonly;                 // Read only flag
  int offset;                   // Offset from the start of the file
  int interleave;               // Interleave data size (for D88 format)
  int over2gb;                  // Over 2GB flag
  struct dos_bpb bpb;           // BIOS Parameter Block
  struct dos_bpb *bpbptr;       // Pointer to BPB
  struct dos_dpb dpb;           // Disk Parameter Block
  char filename[256];           // File name
};

struct loopdrv_param {
  int loopdrv_ver;
  struct dos_devheader *devheader;
  int num_drives;
  struct lodrive drive[MAX_DRIVES];
};

#endif /* _LOOPDRV_H_ */
