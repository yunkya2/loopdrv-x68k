/*
 * Copyright (c) 2024 Yuichi Nakamura (@yunkya2)
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <x68k/dos.h>
#include "loopdrv.h"

//****************************************************************************
// Global variables
//****************************************************************************

// フロッピーディスク用BPB
const struct dos_bpb diskbpb[] = {
#define BPB_2HD 0
  { 1024, 1, 2, 1, 192, 1232, 0xfe, 2, 0, },    // 2HD (1232KB)
#define BPB_2HC 1
  {  512, 1, 2, 1, 224, 2400, 0xfd, 7, 0, },    // 2HC (1200KB)
#define BPB_2HQ 2
  {  512, 1, 2, 1, 224, 2880, 0xfa, 9, 0, },    // 2HQ (1440KB)
#define BPB_2DD640 3
  {  512, 2, 2, 1, 112, 1280, 0xfb, 2, 0, },    // 2DD  (640KB)
#define BPB_2DD720 4
  {  512, 2, 2, 1, 112, 1440, 0xfc, 3, 0, },    // 2DD  (720KB)
};

//****************************************************************************
// Private functions
//****************************************************************************

//
// イメージファイル用のファイルディスクリプタを得る
//
int getimgfd(void)
{
  int maxfd = *(uint16_t *)0x1c6e - 2;  // ファイルディスクリプタ最大値

  for (int i = maxfd; i >= 5; i--) {
    if (_dos_ioctrlgt(i) == -6) {
      return i; // オープンされていないならこのファイルディスクリプタを使う
    }
  }
  return -1;
}

//
// イメージファイルをオープンしてBPBを構築する
//
int openimg(struct lodrive *drive, char *name, int readonly)
{
  drive->status = 0;
  drive->readonly = false;
  drive->offset = 0;
  drive->interleave = 0;

  int fd = -1;
  if (!readonly)
    fd = _dos_open(name, 2);    // RW open
  if (fd < 0) {
    fd = _dos_open(name, 0);    // RO open
    if (fd < 0)
      return -1;
    readonly = true;
  }
  drive->readonly = readonly;

  uint32_t len = _dos_seek(fd, 0, 2);   // ファイルサイズを得る
  uint8_t buf0[1024];
  uint8_t buf1[1024];

  // ディスクのヘッダから判断可能なものを調べる
  if (_dos_seek(fd, 0, 0) < 0)
    goto notfound;
  if (_dos_read(fd, buf0, 1024) < 0)
    goto notfound;

  // SCSI-HDD/MO イメージかどうか
  if (memcmp(buf0, "X68SCSI1", 8) == 0) {
    if (_dos_seek(fd, 0x800, 0) < 0)
      goto notfound;
    if (_dos_read(fd, buf1, 1024) < 0)    // パーティションテーブルを読む
      goto notfound;
    if (memcmp(buf1, "X68K", 4) == 0) {
      if (memcmp(&buf1[16], "Human68k", 8) == 0) {
        uint32_t offset = *(uint32_t *)&buf1[16 + 8];
        if (offset & 0x01000000)
          goto notfound;                  // 使用不可パーティション
        offset &= 0x00ffffff;
        offset *= 1024;
        if (_dos_seek(fd, offset, 0) < 0)
          goto notfound;
        if (_dos_read(fd, buf1, 256) < 0)    // ブートセクタを読む
          goto notfound;
        // ブートセクタ内のBPBを使う
        memcpy(&drive->bpb, &buf1[0x12], sizeof(struct dos_bpb));
        drive->offset = offset;
        return fd;
      }
    }
  }

  // SASI-HDD イメージかどうか
  if (memcmp(buf0, "\x60\x00\x00\xca", 4) == 0) {
    if (_dos_seek(fd, 0x400, 0) < 0)
      goto notfound;
    if (_dos_read(fd, buf1, 1024) < 0)    // パーティションテーブルを読む
      goto notfound;
    if (memcmp(buf1, "X68K", 4) == 0) {
      if (memcmp(&buf1[16], "Human68k", 8) == 0) {
        uint32_t offset = *(uint32_t *)&buf1[16 + 8];
        if (offset & 0x01000000)
          goto notfound;                  // 使用不可パーティション
        offset &= 0x00ffffff;
        offset *= 256;
        if (_dos_seek(fd, offset, 0) < 0)
          goto notfound;
        if (_dos_read(fd, buf1, 256) < 0)    // ブートセクタを読む
          goto notfound;
        // ブートセクタ内のBPBを使う
        memcpy(&drive->bpb, &buf1[0x12], sizeof(struct dos_bpb));
        drive->offset = offset;
        return fd;
      }
    }
  }

  uint32_t extsize = 0;

  // DIM イメージファイルかどうか
  if (memcmp(&buf0[0xab], "DIFC HEADER  ", 14) == 0) {
    extsize = drive->offset = 0x100;
  } else {
    // D88/D68 イメージファイルかどうか
    extsize = 0x2b0;
    int i;
    // トラック情報が正常かどうかチェックする
    for (i = 0; i < 164; i++) {
      uint32_t offset = (buf0[0x20 + i * 4]) +
                        (buf0[0x21 + i * 4] << 8) +
                        (buf0[0x22 + i * 4] << 16) +
                        (buf0[0x23 + i * 4] << 24);
      if (offset == 0)
        continue;
      if (_dos_seek(fd, offset, 0) < 0)
        break;
      if (_dos_read(fd, buf1, 16) < 0)
        goto notfound;
      if (buf1[0] != i / 2 ||
          buf1[1] != i % 2 ||
          buf1[2] != 1)
        break;
      extsize += 0x10 * buf1[4];
    }
    if (i < 164) {
      extsize = 0;        // トラック情報が合わないのでD88/D68ファイルではない
    } else {
      drive->offset = 0x2b0;
      drive->interleave = 0x10;
    }
  }

  // ファイルサイズからBPBを推測する
  switch (len - extsize) {
  case 1232 * 1024:
    drive->bpb = diskbpb[BPB_2HD];
    return fd;
  case 2400 * 512:
    drive->bpb = diskbpb[BPB_2HC];
    return fd;
  case 2880 * 512:
    drive->bpb = diskbpb[BPB_2HQ];
    return fd;
  case 1280 * 512:
    drive->bpb = diskbpb[BPB_2DD640];
    return fd;
  case 1440 * 512:
    drive->bpb = diskbpb[BPB_2DD720];
    return fd;
  }

notfound:
  _dos_close(fd);
  return -2;
}

void help(void)
{
  printf(
"losetup version " GIT_REPO_VERSION "\n"
"使用法:\n"
" losetup                                        : 設定状態表示\n"
" losetup -h                                     : ヘルプ表示\n"
" losetup -D                                     : 全ドライブのアンマウント\n"
" losetup -d ドライブ名                          : イメージファイルのアンマウント\n"
" losetup [-r][-w] ドライブ名                    : ドライブの状態変更\n"
" losetup [-r][-w] ドライブ名 イメージファイル名 : イメージファイルのマウント\n"
"                  (-r 読み込み専用でマウント / -w 読み書き可能でマウント)\n"
  );
  exit(1);
}

void showstat(int unit, struct loopdrv_param *param)
{
  struct lodrive *d = &param->drive[unit];
  for (int i = 0; i < param->num_drives; i++) {
    if (i != unit && unit >= 0)
      continue;
    struct lodrive *d = &param->drive[i];
    if (d->status == 0) {
      printf("%c: --\n", 'A' + d->drive);
    } else {
      int size = d->bpb.sects == 0 ? d->bpb.sectslong : d->bpb.sects;
      size = size * d->bpb.sectbytes / 1024;
      printf("%c: %s %6dkB %s\n",
             'A' + d->drive,
             d->readonly ? "ro" : "rw",
             size,
             d->filename);
    }
  }
}

int detachdrive(int unit, struct loopdrv_param *param)
{
  struct lodrive *d = &param->drive[unit];
  if (d->status != 0) {
    int r = _dos_drvctrl(1, d->drive + 1);  // 排出
    if (r < 0) {
      printf("losetup: %d ドライブ%c:のアンマウントに失敗しました\n", r, 'A' + d->drive);
      return -1;
    }
    _dos_close(d->fd);
    d->status = 0;
    d->fd = -1;
    d->filename[0] = '\0';
  }
  return 0;
}

//****************************************************************************
// Main routine
//****************************************************************************

int main(int argc, char **argv)
{
  struct loopdrv_param *param = NULL;

  _dos_super(0);

  // LOOPDRV デバイスを検索してパラメータを得る
  for (int drive = 1; drive <= 26; drive++) {
    struct dos_dpbptr dpb;
    if (_dos_getdpb(drive, &dpb) < 0)
      continue;
    char *p = (char *)dpb.driver + 14;
    if (memcmp(p, "\x01LOOPDRV", 8) == 0) {
      if (param == NULL)
        param = *(struct loopdrv_param **)(p + 8);
      param->drive[dpb.unit].drive = dpb.drive;
    }
  }
  if (param == NULL) {
    printf("losetup: LOOPDRV.SYSが組み込まれていません\n");
    return 1;
  }
  if (param->loopdrv_ver != LOOPDRV_VERSION) {
    printf("losetup: LOOPDRV.SYSのバージョンが違います\n");
    return 1;
  }
  if (param->drive[0].status < 0) {
    printf("losetup: Human68kのdiskio_read処理の差し替えを検出しました\n"
    "FASTIO.Xなどが常駐している場合は、常駐を解除してください\n");
    return 1;
  }

  // コマンドライン引数を得る

  int unit = -1;
  int readonly = param->default_readonly;
  int changerw = false;
  int detach = false;
  int detachall = false;
  char *filename = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-D") == 0) {
      detachall = true;
    } else if (strcmp(argv[i], "-d") == 0) {
      detach = true;
    } else if (strcmp(argv[i], "-r") == 0) {
      readonly = true;
      changerw = true;
    } else if (strcmp(argv[i], "-w") == 0) {
      readonly = false;
      changerw = true;
    } else if (argv[i][0] == '-') {
      help();
    } else {
      if (unit < 0) {
        int drive = toupper(argv[i][0]);
        if (drive >= 'A' && drive <= 'Z' && argv[i][1] == ':' && argv[i][2] == '\0') {
          for (int j = 0; j < param->num_drives; j++) {
            if (param->drive[j].drive == drive - 'A') {
              unit = j;
              break;
            }
          }
          if (unit < 0) {
            printf("losetup: ドライブ%c:はループバックデバイスではありません\n", drive);
            exit(1);
          }
        } else {
          unit = 0;
          filename = argv[i];
        }
      } else {
        filename = argv[i];
      }
    }
  }

  // -D または -d オプションの処理

  if (detach && unit < 0)
    help();
  if (detachall || detach) {
    for (int i = 0; i < param->num_drives; i++) {
      if (detach && i != unit)
        continue;
      detachdrive(i, param);
    }
    showstat(-1, param);
    exit(0);
  }

  // 引数なしの処理 (状態表示)

  if (unit < 0) {
    showstat(-1, param);
    exit(0);
  }

  // ファイル名なしの場合 (指定ドライブの状態表示)

  if (filename == NULL) {
    if (changerw)
      param->drive[unit].readonly = readonly;
    showstat(unit, param);
    exit(0);
  }

  // ファイル名ありの場合 (マウント処理)

  struct lodrive *d = &param->drive[unit];
  int fd = openimg(d, filename, readonly);
  if (fd == -1) {
    printf("losetup: イメージファイル %s が開けません\n", filename);
    exit(1);
  } else if (fd == -2) {
    printf("losetup: イメージファイルのフォーマットが推測できません\n");
    exit(1);
  }

  if (detachdrive(unit, param) < 0) {
    _dos_close(fd);
    exit(1);
  }

  int dupfd = d->fd;
  if (dupfd < 0) {
    dupfd = getimgfd();
    if (dupfd < 0) {
      _dos_close(fd);
      printf("losetup: ファイルディスクリプタが不足しています\n");
      exit(1);
    }
    d->fd = dupfd;
  }

  _dos_dup2(fd, dupfd);
  _dos_close(fd);
  d->status = 1;
  strcpy(d->filename, filename);

  // プロセス終了時にイメージファイルを開いているファイルディスクリプタがクローズされないようにする

  struct dos_psp *psp = _dos_getpdb();
  psp->handle[dupfd / 8] &= ~(1 << (dupfd % 8));

  showstat(unit, param);
  return 0;
}
