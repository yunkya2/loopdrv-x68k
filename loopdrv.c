#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <x68k/dos.h>
#include "loopdrv.h"

//****************************************************************************
// Global variables
//****************************************************************************

struct dos_req_header *reqheader;   // Human68kからのリクエストヘッダ

struct loopdrv_param g_param;
struct dos_bpb *bpblist[MAX_DRIVES];

struct dos_bpb default_bpb = {
  1024, 1, 1, 1, 1, 1, 0x01, 1, 0,
};

int _vernum = 0x302;

//****************************************************************************
// for debugging
//****************************************************************************

#ifdef DEBUG
#include <x68k/iocs.h>
char heap[1024];                // temporary heap for debug print
void *_HSTA = heap;
void *_HEND = heap + 1024;
void *_PSP;

void DPRINTF(char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  vsiprintf(buf, fmt, ap);
  va_end(ap);
  _iocs_b_print(buf);
}
#else
#define DPRINTF(...)
#endif

//****************************************************************************
// Private function
//****************************************************************************

static int rw_sector(struct dos_req_header *req, int (*func)(int, char *, int))
{
  int r;
  struct lodrive *d = &g_param.drive[req->unit];

  if (d->status <= 0) {
    goto error;
  }

  int sect = d->bpb.sectbytes;
  uint32_t pos = d->offset + (uint32_t)req->fcb * (d->interleave + sect);
  uint8_t *buf = req->addr;

  r = _dos_seek(d->fd, pos, 0);
  if (r < 0) {
    goto error;
  }

  if (d->interleave == 0) {
    r = func(d->fd, buf, req->status * sect);
    if (r < 0) {
      goto error;
    }
  } else {
    for (int i = 0; i < req->status; i++) {
      r = _dos_seek(d->fd, d->interleave, 1);
      if (r < 0) {
        goto error;
      }
      r = func(d->fd, buf, sect);
      if (r < 0) {
        goto error;
      }
      buf += sect;
    }
  }
  return 0;

error:
  d->status = 0;
  return 0x1002;      // Drive not ready
}

static int my_atoi(char *p)
{
  int res = 0;
  while (*p >= '0' && *p <= '9') {
    res = res * 10 + *p++ - '0';
  }
  return res;
}

static int check_patch(void)
{
  if (*(uint32_t *)0xb68e == 0x00e9ba)
    return 0;
  // FASTIO.Xが組み込まれるなどでHuman68kのdiskio_read処理が差し替えられている
  // 場合は再帰呼び出し対応修正が効かなくなるため、デバイスの動作を停止する
  for (int i = 0; i < g_param.num_drives; i++) {
    struct lodrive *d = &g_param.drive[i];
    d->status = -1;
  }
  return -1;
}

//****************************************************************************
// Device driver interrupt rountine
//****************************************************************************

int interrupt(void)
{
  uint16_t err = 0;
  struct dos_req_header *req = reqheader;

  DPRINTF("[%d]", req->command);

  switch (req->command) {
  case 0x00: /* init */
  {
    _dos_print("\r\nLoopback device driver for X680x0 version " GIT_REPO_VERSION "\r\n");

    int ver = _dos_vernum();
    if ((ver & 0xffff) != 0x0302) {
      _dos_print("Human68kのバージョンがv3.02でないため組み込めません\r\n");
      err = 0x700d;
      break;
    }

    int units = 1;
    g_param.loopdrv_ver = LOOPDRV_VERSION;
    g_param.default_readonly = false;

    char *p = (char *)req->status;
    p += strlen(p) + 1;
    while (*p != '\0') {
      if (*p == '/' || *p =='-') {
        p++;
        switch (*p | 0x20) {
        case 'd':         // /d<units> .. ドライブ数設定
        case 'u':         // /u<units> .. ユニット数設定
          p++;
          units = my_atoi(p);
          if (units < 1)
            units = 1;
          else if (units > MAX_DRIVES)
            units = MAX_DRIVES;
          break;
        case 'r':         // /r .. readonlyモードをデフォルトにする
          g_param.default_readonly = true;
          break;
        }
      }
      p += strlen(p) + 1;
    }

    req->attr = units;
    g_param.num_drives = units;
    for (int i = 0; i < units; i++) {
      struct lodrive *d = &g_param.drive[i];
      d->status = 0;
      d->fd = -1;
      d->bpb = default_bpb;
      bpblist[i] = &d->bpb;
      d->filename[0] = '\0';
    }
    req->status = (uint32_t)&bpblist;

    _dos_print("ドライブ");
    _dos_putchar('A' + *(uint8_t *)&req->fcb);
    if (req->attr > 1) {
      _dos_print(":-");
      _dos_putchar('A' + *(uint8_t *)&req->fcb + req->attr - 1);
    }
    _dos_print(":でループバックデバイスが利用可能です\r\n");

    // Human68kのdiskio処理にパッチを当てる
    extern char diskio_read_fix;
    *(uint16_t *)0xeac2 = 0x4ef9;   // jmp
    *(uint32_t *)0xeac4 = (uint32_t)&diskio_read_fix;
    extern char diskio_flush_fix;
    *(uint16_t *)0xebd0 = 0x4ef9;   // jmp
    *(uint32_t *)0xebd2 = (uint32_t)&diskio_flush_fix;
    extern char diskio_ioread_fix;
    *(uint16_t *)0xec34 = 0x4ef9;   // jmp
    *(uint32_t *)0xec36 = (uint32_t)&diskio_ioread_fix;

    extern char _end;
    req->addr = &_end;

    break;
  }

  case 0x01: /* disk check */
  {
    if (check_patch() < 0) {
      *(int8_t *)&req->addr = -1;
      break;
    }

    int res;
    switch (g_param.drive[req->unit].status) {
      case 0:
        res = -1;
        break;
      case 1:
        res = -1;
        g_param.drive[req->unit].status = 2;
        break;
      default:
        res = 1;
        break;
    }
    *(int8_t *)&req->addr = res;
    break;
  }

  case 0x02: /* rebuild BPB */
  {
    DPRINTF("Rebuild BPB unit %d\r\n", req->unit);
    req->status = (uint32_t)&bpblist[req->unit];
    break;
  }

  case 0x05: /* drive control & sense */
  {
    DPRINTF("DriveControl %d\r\n", req->attr);
    if (check_patch() < 0) {
      req->attr = 0x04;
      break;
    }

    if (g_param.drive[req->unit].status == 0) {
      req->attr = 0x04;
    } else {
      req->attr = 0x02;
      if (g_param.drive[req->unit].readonly)
        req->attr |= 0x08;
    }
    break;
  }

  case 0x04: /* read */
  {
    DPRINTF("Read: #%08x %08x\r\n", (int)req->fcb, (int)req->status);
    err = rw_sector(req, _dos_read);
    break;
  }

  case 0x08: /* write */
  case 0x09: /* write+verify */
  {
    DPRINTF("Write: #%08x %08x\r\n", (int)req->fcb, (int)req->status);
    if (g_param.drive[req->unit].readonly) {
      err = 0x100d;   // Write protected
      break;
    }
    err = rw_sector(req, (int (*)(int, char *, int))_dos_write);
    break;
  }

  case 0x03: /* ioctl in */
  {
    DPRINTF("Ioctl in\r\n");
    break;
  }

  case 0x0c: /* ioctl out */
  {
    DPRINTF("Ioctl out\r\n");
    break;
  }

  default:
    DPRINTF("Invalid command 0x%02x\r\n", req->command);
    err = 0x1003;   // Invalid command
    break;
  }

  req->errl = err & 0xff;
  req->errh = err >> 8;
  return err;
}

//****************************************************************************
// Dummy program entry
//****************************************************************************

void _start(void)
{}
