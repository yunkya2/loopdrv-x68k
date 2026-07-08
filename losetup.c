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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <x68k/dos.h>
#include "loopdrv.h"

//****************************************************************************
// Macros and definitions
//****************************************************************************

// Human68k work area

#define MEMBLK_TOP    (*(char **)0x1c20)                // 先頭のメモリブロック
#define CURDIR_TABLE  (*(struct dos_curdir **)0x1c38)   // カレントディレクトリテーブル
#define MAXFILES      (*(uint16_t *)0x1c6e)             // ファイルディスクリプタ最大値
#define BUFFERS_SEC   (*(uint16_t *)0x1c70)             // BUFFERSのセクタサイズ(第2引数)
#define LASTDRIVE     (*(uint8_t *)0x1c73)              // LASTDRIVEの値
#define CONNDRIVE     (*(uint8_t *)0x1c75)              // 接続ドライブ数-1
#define DRVXTBL       ((uint8_t *)0x1c7e)               // ドライブ交換テーブル

//****************************************************************************
// Global variables
//****************************************************************************

extern struct dos_devheader devheader;  // Human68kのデバイスヘッダ
extern struct loopdrv_param g_param;    // ループバックドライバのパラメータ

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

// イメージファイル用のファイルディスクリプタを得る
int getimgfd(void)
{
  int maxfd = MAXFILES - 2;   // ファイルディスクリプタ最大値

  for (int i = maxfd; i >= 5; i--) {
    if (_dos_ioctrlgt(i) == -6) {
      return i; // オープンされていないならこのファイルディスクリプタを使う
    }
  }
  return -1;
}

// 入力パスを絶対パスに正規化して out に格納する
int getfullpath(const char *name, char *out, size_t outsize)
{
  struct dos_nameckbuf ncb;
  if (_dos_nameck(name, &ncb) < 0) {
    return -1;
  }

  int len = snprintf(out, outsize, "%c%c%s%s%s",
                     ncb.drive[0], ncb.drive[1], ncb.path, ncb.name, ncb.ext);
  if (len < 0 || (size_t)len >= outsize) {
    return -1;
  }
  return 0;
}

// 指定したファイルディスクリプタが終了時にクローズされないようにする
void keepfd(int fd)
{
  struct dos_psp *psp = _dos_getpdb();
  psp->handle[fd / 8] &= ~(1 << (fd % 8));
}

// イメージファイルをオープンしてBPBを構築する
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

  uint8_t buf0[1024];
  uint8_t buf1[1024];
  uint32_t len = _dos_seek(fd, 0, 2);   // ファイルサイズを得る

  if (!drive->over2gb) {
    if ((int)len < 0)
      goto toobig;      // 2GBを超えるファイルはデフォルトではエラーにする
  } else {
    if (len > 0xffffff00)
      goto toobig;      // 2GB超えを許可する場合、2GB-256を超えるファイルはエラーにする
  }

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

  // IBM SuperFD format MOイメージかどうか
  if (buf0[0] == 0xeb && buf0[510] == 0x55 && buf0[511] == 0xaa) {
    // ブートセクタ内のBPBを使う
    drive->bpb.sectbytes = buf0[0x0b] + (buf0[0x0c] << 8);
    drive->bpb.sectclust = buf0[0x0d];
    drive->bpb.resvsects = buf0[0x0e] + (buf0[0x0f] << 8);
    drive->bpb.fatnum = buf0[0x10] | 0x80;  // Intel FAT
    drive->bpb.rootent = buf0[0x11] + (buf0[0x12] << 8);
    drive->bpb.sects = buf0[0x13] + (buf0[0x14] << 8);
//    drive->bpb.mediabyte = buf0[0x15];
    drive->bpb.mediabyte = 0xf6;  // MOに固定
    drive->bpb.fatsects = buf0[0x16] + (buf0[0x17] << 8);
    drive->bpb.sectslong = buf0[0x20] + (buf0[0x21] << 8) +
                           (buf0[0x22] << 16) + (buf0[0x23] << 24);

    return fd;
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
toobig:
  _dos_close(fd);
  return -3;
}

// BPBからDPBを構築する
int bpb2dpb(struct dos_bpb *bpb, struct dos_dpb *dpb)
{
  if (BUFFERS_SEC < bpb->sectbytes) {
    printf("1セクタあたりのバイト数が大きすぎます\n");
    return -1;
  }

  int bytes;
  int shift;

  dpb->sectbytes = bpb->sectbytes;
  bytes = bpb->sectbytes - 1;
  shift = 0;
  while (bytes > 0) {
    bytes >>= 1;
    shift++;
  }
  dpb->sbshift = shift;

  dpb->sectclust = bpb->sectclust - 1;
  bytes = bpb->sectclust - 1;
  shift = 0;
  while (bytes > 0) {
    bytes >>= 1;
    shift++;
  }
  if (bpb->fatnum & 0x80) {
    shift |= 0x80;  // Intel FATの場合はbit7を立てる
  }
  dpb->fatnum = bpb->fatnum & 0x7f;
  dpb->csshift = shift;

  dpb->fatsect = bpb->resvsects;
  dpb->fatsects = bpb->fatsects;

  dpb->rootent = bpb->rootent;
  dpb->rootsect = dpb->fatsects * dpb->fatnum + dpb->fatsect;

  int rootsects = (dpb->rootent * 32 + dpb->sectbytes - 1) / dpb->sectbytes;
  dpb->datasect = dpb->rootsect + rootsects;

  int sects = bpb->sects ? bpb->sects : bpb->sectslong;
  dpb->totalclu = ((sects - dpb->datasect) >> dpb->csshift) + 3;
  if (dpb->totalclu > 0xfff8) {
    printf("losetup: 総クラスタ数が大きすぎます\n");
    return -1;
  }

  dpb->mediabyte = bpb->mediabyte;
  dpb->fatfindpos = 2;

  dpb->schdir_firstfat = 0;
  dpb->schfil_firstfat = 0;
  return 0;
}

//----------------------------------------------------------------------------

// 次のデバイスが next となるデバイスヘッダを探す
static struct dos_devheader *find_devheader(struct dos_devheader *next)
{
  // Human68kからNULデバイスドライバを探す
  char *p = MEMBLK_TOP;   // 先頭のメモリブロック
  while (memcmp(p, "NUL     ", 8) != 0) {
    p += 2;
  }

  // デバイスドライバのリンクをたどって next の前のデバイスヘッダを探す
  struct dos_devheader *devh = (struct dos_devheader *)(p - 14);
  while (devh != (struct dos_devheader *)-1) {
    if (devh->next == next) {
      return devh;
    }
    devh = devh->next;
  }
  return NULL;
}

//----------------------------------------------------------------------------

// 指定したドライブ番号のLOOPDRVパラメータを得る
struct loopdrv_param *getloparam(int drive, int *unit)
{
  // 指定されたドライブ番号のDPBを得る
  struct dos_dpbptr dpbptr;
  if (_dos_getdpb(drive + 1, &dpbptr) < 0)
    return NULL;

  if (unit)
    *unit = dpbptr.unit;

  // DPBのドライバがLOOPDRVかどうかを確認する
  struct dos_devheader *devheader = (struct dos_devheader *)dpbptr.driver;
  if (memcmp(devheader->name, CONFIG_DEVNAME, 8) != 0)
    return NULL;

  // LOOPDRVのパラメータを得る
  struct loopdrv_param *param = (struct loopdrv_param *)devheader->param;
  if (param->loopdrv_ver != LOOPDRV_VERSION) {
    return NULL;    // LOOPDRVのバージョンが違う
  }

  return param;
}

// 指定したドライブ番号のLOOPDRVドライブを得る
struct lodrive *getlodrive(int drive)
{
  int unit;

  struct loopdrv_param *param = getloparam(drive, &unit);
  if (param == NULL)
    return NULL;

  return &param->drive[unit];
}

//----------------------------------------------------------------------------

// 指定したドライブ番号のLOOPDRVドライブの状態を表示する
void showstat(int drive)
{
  for (int i = 0; i < 26; i++) {
    if (drive >= 0 && drive != i)
      continue;
    struct lodrive *d = getlodrive(i);
    if (d == NULL)
      continue;

    if (d->status == 0) {
      printf("%c: --\n", 'A' + i);
    } else {
      int size = d->bpb.sects == 0 ? d->bpb.sectslong : d->bpb.sects;
      size = size * d->bpb.sectbytes / 1024;
      printf("%c: %s %6ukB %s\n",
             'A' + i,
             d->readonly ? "ro" : "rw",
             size,
             d->filename);
    }
  }
}

// ループバックドライブをアンマウントする
int umountdrive(int drive, bool ignore_err)
{
  struct lodrive *d = getlodrive(drive);
  if (d == NULL) {
    if (ignore_err) {
      return 0;
    } else {
      printf("ドライブ %c: はループバックドライブではありません\n", 'A' + drive);
      return -1;
    }
  }

  if (d->status != 0) {
    int r = _dos_drvctrl(1, drive + 1);  // 排出
    if (r < 0) {
      printf("ドライブ %c: をアンマウントできません\n", 'A' + drive);
      return -1;
    }
    _dos_close(d->fd);
    d->status = 0;
    d->fd = -1;
    d->filename[0] = '\0';
    return 1;
  }

  return 0;
}

// ループバックドライブの読み書き状態を変更する
int changerwdrive(struct lodrive *d, int readonly)
{
  // 未マウント時はフラグ変更のみ行う
  if (d->status == 0) {
    d->readonly = readonly;
    return 0;
  }

  // 読み込み専用への変更はフラグ変更のみ
  if (readonly) {
    d->readonly = true;
    return 0;
  }

  // 読み書き可能への変更の場合はファイルオープンのやり直しが必要
  if (!d->readonly) {
    return 0;
  }

  int fd = _dos_open(d->filename, 2);    // RW open
  if (fd < 0) {
    return -1;
  }
  if (_dos_dup2(fd, d->fd) < 0) {
    _dos_close(fd);
    return -1;
  }
  _dos_close(fd);
  d->readonly = false;

  // プロセス終了時にイメージファイルを開いているファイルディスクリプタがクローズされないようにする
  keepfd(d->fd);

  return 0;
}

// loopdrvドライブを削除する
int detachdrive(int drive)
{
  struct loopdrv_param *param = getloparam(drive, NULL);
  if (param == NULL)
      return -1;
  for (int i = 0; i < param->num_drives; i++) {
    if (param->drive[i].status != 0) {
      return 0;   // 他のドライブがマウントされている場合はLOOPDRVを残す
    }
  }

  // デバイスドライバのリンクリストからloopdrvを外す
  struct dos_devheader *prev = find_devheader(param->devheader);
  if (prev != NULL) {
    prev->next = param->devheader->next;
  }

  // 常駐しているloopdrvのドライブを削除する
  int first = 1;
  for (int drv = 0; drv < 26; drv++) {
    struct dos_curdir *curdir = &CURDIR_TABLE[(int)DRVXTBL[drv]];
    if (curdir->type != 0x40 || curdir->dpb->devheader != param->devheader) {
      continue;
    }

    // Human68kのカレントディレクトリテーブルからloopdrvを外す
    curdir->type = 0;
    for (int i = 0; i < 26; i++) {
      if (CURDIR_TABLE[i].type == 0x40 &&
          CURDIR_TABLE[i].dpb->next == curdir->dpb) {
          CURDIR_TABLE[i].dpb->next = curdir->dpb->next;
      }
    }

    // 接続ドライブ数を減少
    CONNDRIVE--;
  }

  // 常駐部を解放する
  _dos_mfree((char *)param->devheader - 0xf0);

  return 0;
}

// loopdrvドライブを追加する
int attachdrive(int drive, struct dos_dpb *dpb, struct dos_devheader *devheader)
{
  int realdrv = DRVXTBL[drive];
  struct dos_curdir *curdir = &CURDIR_TABLE[realdrv];

  // DPBを初期化
  dpb->unit = 0;
  dpb->drive = realdrv;
  dpb->devheader = devheader;
  dpb->next = (struct dos_dpb *)-1;

  // Human68kのDPBリストにDPBを繋ぐ
  struct dos_dpb *prev_dpb = NULL;
  for (int i = 0; i < realdrv; i++) {
    if (CURDIR_TABLE[i].type == 0x40) {
      prev_dpb = CURDIR_TABLE[i].dpb;
    }
  }
  if (prev_dpb != NULL) {
    dpb->next = prev_dpb->next;
    prev_dpb->next = dpb;
  }

  // Human68kのカレントディレクトリテーブルを設定する
  curdir->drive = 'A' + realdrv;
  curdir->coron = ':';
  curdir->path[0] = '\t';
  curdir->path[1] = '\0';
  curdir->type = 0x40;
  curdir->dpb = dpb;
  curdir->curfat = (int)-1;
  curdir->pathlen = 2;

  // デバイスドライバのリンクリストにsmbfsを繋ぐ
  struct dos_devheader *prev = find_devheader((struct dos_devheader *)-1);
  if (prev != NULL) {
    prev->next = devheader;
  }

  // 接続ドライブ数を増加
  CONNDRIVE++;

  return 0;
}

//----------------------------------------------------------------------------

void help(void)
{
  printf(
"losetup version " GIT_REPO_VERSION "\n"
"使用法:\n"
" losetup                                              : マウント状態表示\n"
" losetup -h                                           : ヘルプ表示\n"
" losetup -D                                           : 全ドライブのアンマウント\n"
" losetup -d ドライブ名                                : イメージファイルのアンマウント\n"
" losetup [-r][-w] ドライブ名                          : ドライブの状態変更\n"
" losetup [-r][-w][-f] イメージファイル名 [ドライブ名] : イメージファイルのマウント\n"
"  (-r 読み込み専用でマウント / -w 読み書き可能でマウント / -f 2GB以上のイメージファイルを許可)\n"
  );
  exit(1);
}

//****************************************************************************
// Main routine
//****************************************************************************

int main(int argc, char **argv)
{
  // コマンドライン引数を得る

  int readonly = true;
  int changerw = false;
  int detach = false;
  int detachall = false;
  int over2gb = false;
  int drive = -1;
  char *filename = NULL;

  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'D':
      case 'U':
        detachall = true;
        break;
      case 'd':
      case 'u':
        detach = true;
        break;
      case 'r':
        readonly = true;
        changerw = true;
        break;
      case 'w':
        readonly = false;
        changerw = true;
        break;
      case 'f':
        over2gb = true;
        break;
      default:
        help();
      }
    } else {
      int d = toupper(argv[i][0]);
      if (d >= 'A' && d <= 'Z' && argv[i][1] == ':' && argv[i][2] == '\0') {
        drive = d - 'A';
      } else {
        filename = argv[i];
      }
    }
  }

  int ver = _dos_vernum();
  if ((ver & 0xffff) != 0x0302) {
    _dos_print("Human68kのバージョンがv3.02でないため実行できません\r\n");
    return 1;
  }

  _dos_super(0);

  if (*(uint32_t *)0xb68e != 0x00e9ba) {
    _dos_print("Human68kのdiskio_read処理の差し替えを検出しました\r\n"
    "FASTIO.Xなどが常駐している場合は、常駐を解除してください\r\n");
    return 1;
  }

  struct loopdrv_param *param = &g_param;
  param->loopdrv_ver = LOOPDRV_VERSION;
  param->num_drives = 1;
  param->devheader = &devheader;

  // -D または -d オプションの処理

  if (detachall) {
    bool any_mounted;
    bool any_error;

    do {    // アンマウントできるドライブがなくなるまで繰り返す
      any_mounted = false;
      any_error = false;
      for (int i = 25; i >= 0; i--) {
        int res = umountdrive(i, true);
        if (res < 0) {
          any_error = true;
        } else if (res == 0) {
          continue;
        } else {
          detachdrive(i);
          any_mounted = true;
        }
      }
    } while (any_mounted);

    if (any_error) {
      printf("一部のループバックドライブをアンマウントできませんでした\n");
      exit(1);
    } else {
      printf("すべてのループバックドライブをアンマウントしました\n");
      exit(0);
    }
  }

  if (detach) {
    if (drive < 0) {
      help();
    }
    if (umountdrive(drive, false) < 0 || detachdrive(drive) < 0) {
      exit(1);
    }
    printf("ドライブ %c: をアンマウントしました\n", 'A' + drive);
    exit(0);
  }

  // ファイル名なしの場合

  if (filename == NULL) {
    if (drive < 0) {
      // ドライブ名指定もない場合はマウント状態を表示する
      showstat(-1);
    } else {
      // ドライブ名指定がある場合
      int unit;
      param = getloparam(drive, &unit);
      if (param == NULL) {
        printf("ドライブ %c: はループバックドライブではありません\n", 'A' + drive);
        exit(1);
      }

      if (changerw) {
        // ドライブの読み書き状態を変更する
        struct lodrive *d = &param->drive[unit];
        if (changerwdrive(d, readonly) < 0) {
          printf("ドライブ %c: の読み書き状態を変更できません\n", 'A' + drive);
          exit(1);
        }
        printf("ドライブ %c: の読み書き状態を変更しました\n", 'A' + drive);
      } else {
        // ドライブの状態を表示する
        showstat(drive);
      }
    }
    exit(0);
  }

  // ファイル名ありの場合 (マウント処理)

  struct lodrive *d = &param->drive[0];
  d->over2gb = over2gb;
  int fd = openimg(d, filename, readonly);
  if (fd == -1) {
    printf("イメージファイル %s が開けません\n", filename);
    exit(1);
  } else if (fd == -2) {
    printf("イメージファイルのフォーマットが推測できません\n");
    exit(1);
  } else if (fd == -3) {
    printf("イメージファイルが大きすぎます\n");
    exit(1);
  }

  memset(&d->dpb, 0, sizeof(struct dos_dpb));
  if (bpb2dpb(&d->bpb, &d->dpb) < 0) {
    _dos_close(fd);
    printf("イメージファイルのフォーマットが不正です\n");
    exit(1);
  }

  // マウント先のドライブ番号を決定する

  if (drive < 0) {
    // ドライブ名指定がない場合は空きドライブを探す
    for (int i = 0; i < 26; i++) {
      int realdrv = DRVXTBL[i];
      struct dos_curdir *curdir = &CURDIR_TABLE[realdrv];
      if (curdir->type == 0 && realdrv <= LASTDRIVE) {
        drive = i;
        break;
      }
    }
    if (drive < 0) {
      printf("割り当て可能なドライブがありません\n");
      exit(1);
    }
  } else {
    // ドライブ名指定がある場合はそのドライブを使う
    int realdrv = DRVXTBL[drive];
    struct dos_curdir *curdir = &CURDIR_TABLE[realdrv];
    if (curdir->type == 0 && realdrv <= LASTDRIVE) {
      // 指定されたドライブが空きドライブの場合はそのまま使う
    } else {
      // 指定されたドライブをアンマウントして再利用する
      if (umountdrive(drive, false) < 0) {
        exit(1);
      }

      int unit;
      param = getloparam(drive, &unit);
      struct lodrive *newd = &param->drive[unit];
      newd->bpb = d->bpb;
      newd->readonly = d->readonly;
      newd->offset = d->offset;
      newd->interleave = d->interleave;
      newd->over2gb = d->over2gb;
      d = newd;
    }
  }

  int dupfd = getimgfd();
  if (dupfd < 0) {
    _dos_close(fd);
    printf("ファイルディスクリプタが不足しています\n");
    exit(1);
  }
  d->fd = dupfd;

  // イメージファイル用ディスクリプタに複製して元のfdを閉じる
  _dos_dup2(fd, dupfd);
  _dos_close(fd);
  d->status = 1;
  if (getfullpath(filename, d->filename, sizeof(d->filename)) < 0) {
    // 正規化できない場合でも、従来どおり指定文字列を保持して動作継続する
    strncpy(d->filename, filename, sizeof(d->filename) - 1);
    d->filename[sizeof(d->filename) - 1] = '\0';
  }

  // プロセス終了時にイメージファイルを開いているファイルディスクリプタがクローズされないようにする
  keepfd(d->fd);

  if (param != &g_param) {
    exit(0);  // 常駐済みのloopdrvの設定を変更して終了
  }

  // Human68kのdiskioドライバにリエントラント対応パッチを当てる
  void apply_iopatch(void);
  apply_iopatch();

  attachdrive(drive, &d->dpb, &devheader);
  printf("ドライブ %c: にイメージファイル %s をマウントしました\n", 'A' + drive, filename);

#ifdef DEBUG
  // ヒープ領域の末尾までを常駐して終了する
  extern char _HEND;
  _dos_keeppr((int)&_HEND - (int)&devheader, 0);
#else
  // loopdrvの常駐部のみを常駐して終了する
  extern char _start;
  _dos_keeppr((int)&_start - (int)&devheader, 0);
#endif

  return 0;
}
