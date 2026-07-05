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

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <x68k/dos.h>
#include "loopdrv.h"

//****************************************************************************
// Global variables
//****************************************************************************

struct dos_req_header *reqheader;   // Human68kからのリクエストヘッダ
struct loopdrv_param g_param;       // ループバックドライバのパラメータ

//****************************************************************************
// for debugging
//****************************************************************************

#ifdef DEBUG
#include <stdarg.h>
#include <x68k/iocs.h>
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

  uint32_t n;
  n = _dos_seek(d->fd, pos, 0);
  if (n != pos) {
    if (!d->over2gb)
      goto error;       // 移動後の位置が移動先と異なる場合はエラー
    // 2GB overを許容する場合、2回に分けてシークしてみる
    n = _dos_seek(d->fd, 0x7fffffff, 0);
    if (n != 0x7fffffff)
      goto error;
    n = _dos_seek(d->fd, pos - 0x7fffffff, 1);
    if (n != pos)
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
    err = 0x700d;
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
    g_param.drive[req->unit].bpbptr = &g_param.drive[req->unit].bpb;
    req->status = (uint32_t)&g_param.drive[req->unit].bpbptr;
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

  return err;
}
