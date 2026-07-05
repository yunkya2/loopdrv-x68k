/*
 * Copyright (c) 2026 Yuichi Nakamura (@yunkya2)
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
#include <string.h>
#include <x68k/dos.h>

//****************************************************************************
// Patch data for disk I/O driver reentrance
//****************************************************************************

// $00eac8-$00eb54
// diskio共通read処理(readfat/readdir/makedir/readfile/makefile)のリエントラント対応
// バッファの読み書きの後はリンクリストが変化している可能性があるので、
// 読み書き後にリンクリストの更新を行っていたのをリンクリストを更新してから読み書きするように変更する
/*
        変更前                                  変更後                          差分
L00eac2:                                L00eac2:
        movem.l d1-d3/a2,-(a7)                  movem.l d1-d3/a2,-(a7)
        rol.l   #8,d1                           rol.l   #8,d1
        move.b  $0000(a0),d1                    move.b  (a0),d1                 * 短縮
        ror.l   #8,d1                           ror.l   #8,d1
                                        L00eace:
        move.l  $0024(a5),d2                    move.l  $0024(a5),d2
L00ead2:                                L00ead2:                                ; I/Oバッファからドライブ+セクタ番号が一致するものを探す
        movea.l d2,a1                           movea.l	d2,a1
        move.l  (a1),d2                         move.l  (a1),d2
                                                lea.l   $0004(a1),a2            * 短縮 ($0004(a1)が何度か出てくるので) (フラグ変化なし)
        bmi     L00eaec                         bmi     L00eaec
        cmp.l   $0004(a1),d1                    cmp.l   (a2),d1                 * 短縮
        bne     L00ead2                         bne     L00ead2
        move.l  $0008(a1),d3                    move.l  $0008(a1),d3            ; 一致するバッファをリンクリストから取り除く
        bmi     L00eb4a                         bmi     L00eb4a
        movea.l d2,a2                           movea.l d2,a2
        move.l  d3,$0008(a2)                    move.l  d3,$0008(a2)
        bra     L00eb30                         bra     L00eb30

L00eaec:                                L00eaec:
        cmp.l   $0004(a1),d1                    cmp.l   (a2),d1                 * 短縮
        beq     L00eb30                         beq     L00eb30
        tst.b   $0004(a1)                       tst.b   (a2)                    * 短縮
        bmi     L00eb02                         bmi     L00eb02
        bclr.b  #$07,$000c(a1)                  bclr.b  #$07,$000c(a1)          ; 一致するバッファがないのでリストの末尾をflushして使用する
        beq     L00eb02                         beq     L00eb02
        bsr     L00eb54                         bsr     L00eb54                 ; dirtyなバッファをflushする ※この際にリンクリストが変化する可能性あり
                                                bra     L00eace                 * flushによってリンクリストが変化しているので先頭から探しなおす
L00eb02:                                L00eb02:
        move.l  a0,d3                           move.l  a0,d3                   ; DPBアドレス
        rol.l   #8,d3                           rol.l   #8,d3
        move.b  d0,d3                           move.b  d0,d3                   ; バッファ状態フラグを設定
        ror.l   #8,d3                           ror.l   #8,d3
                                                move.l  d1,$0004(a1)            * セクタの内容を読み込んでからリンクリストを更新していたのを
                                                move.l  d3,$000c(a1)            * リンクリストの更新を先に行うように変更する
                                                bsr.s   L00eb30_main            * リンクリスト更新処理をサブルーチン化
        cmp.w   #$0100,d0                       cmp.w   #$0100,d0               ; セクタ読み込みが必要かどうか
        bcc     L00eb28                         bcc     L00eb4a                 * 既にリンクリストは更新済みなのでリターンするのみ
        movem.l d1-d3,-(a7)                                                     * 既にd1-d3は保存されているので保存不要
        and.l   #$00ffffff,d1                   and.l   #$00ffffff,d1
        lea.l   $0010(a1),a2                    lea.l   $0010(a1),a2
        moveq.l #$01,d2                         moveq.l #$01,d2
        jsr     $0000(a5)                       jsr     (a5)                    * 短縮  ; セクタの内容を読み込む ※この際にリンクリストが変化する可能性あり
        movem.l (a7)+,d1-d3                                                     * リターン時にd1-d3が復元されているので復帰不要
                                                bra     L00eb4a                 * 既にリンクリストは更新済みなのでリターンするのみ
L00eb28:
        move.l  d1,$0004(a1)                                                    * 処理順の入れ替え
        move.l  d3,$000c(a1)                                                    *
L00eb30:                                L00eb30:
                                                bsr.s   L00eb30_main            * リンクリスト更新処理をサブルーチン化
        movea.l $0008(a1),a2                                                    * 
        move.l  d2,(a2)                                                         * 
        movea.l $0024(a5),a2                                                    * 
        move.l  a1,$0008(a2)                                                    * 
        move.l  a2,(a1)                                                         * 
        moveq.l #$ff,d3                                                         * 
        move.l  d3,$0008(a1)                                                    * 
        move.l  a1,$0024(a5)                                                    * 
L00eb4a:                                L00eb4a:
        lea.l   $0010(a1),a1                    lea.l   $0010(a1),a1
        movem.l (a7)+,d1-d3/a2                  movem.l (a7)+,d1-d3/a2
        rts                                     rts

                                        L00eb30_main:                           * サブルーチン化されたリンクリスト更新処理
                                                movea.l $0008(a1),a2            * ; バッファをLRUリストの先頭に移動する
                                                move.l  d2,(a2)                 *
                                                movea.l $0024(a5),a2            *
                                                move.l  a1,$0008(a2)            *
                                                move.l  a2,(a1)                 *
                                                moveq.l #$ff,d3                 *
                                                move.l  d3,$0008(a1)            *
                                                move.l  a1,$0024(a5)            *
                                                rts                             *


                                        L00eb54_00ebd8:                         * diskio_flushから使われる処理
                                                pea.l   L00ebd8(pc)             * バッファをフラッシュ後、L00ebd8に戻る
                                        L00eb54:
                                                * (この先バッファフラッシュ処理)
*/

static uint16_t iopatch1[] = {
0x1210, 0xe099, 0x242D, 0x0024, 0x2242, 0x2411, 0x45E9, 0x0004,
0x6B12, 0xB292, 0x66F2, 0x2629, 0x0008, 0x6B46, 0x2442, 0x2543,
0x0008, 0x603C, 0xB292, 0x6738, 0x4A12, 0x6B0C, 0x08A9, 0x0007,
0x000C, 0x6704, 0x6156, 0x60CC, 0x2608, 0xE19B, 0x1600, 0xE09B,
0x2341, 0x0004, 0x2343, 0x000C, 0x6122, 0xB07C, 0x0100, 0x6412,
0xC2BC, 0x00FF, 0xFFFF, 0x45E9, 0x0010, 0x7401, 0x4E95, 0x6002,
0x610A, 0x43E9, 0x0010, 0x4CDF, 0x040E, 0x4E75, 0x2469, 0x0008,
0x2482, 0x246D, 0x0024, 0x2549, 0x0008, 0x228A, 0x76FF, 0x2343,
0x0008, 0x2B49, 0x0024, 0x4E75, 0x487A, 0x0086
};


// $00ebf0-$00ebf4
// diskio_flush処理のリエントラント対応
// バッファフラッシュ後はリンクリストが変化している可能性があるので先頭から探しなおすように変更する
/*
        変更前                                  変更後                          差分
L00ebd0:                                L00ebd0:
        movem.l d0-d2/a1,-(a7)                  movem.l d0-d2/a1,-(a7)
        move.l  a0,d2                           move.l  a0,d2
        lsl.l   #8,d2                           lsl.l   #8,d2
                                        L00ebd8:
        move.l  $0024(a5),d0                    move.l  $0024(a5),d0
L00ebdc:                                L00ebdc:                                ; I/OバッファからDPBが同じでdirtyなバッファを探す
        movea.l d0,a1                           movea.l d0,a1
        tst.b   $0004(a1)                       tst.b   $0004(a1)
        bmi     L00ebf4                         bmi     L00ebf4                 ; バッファは未使用
        move.l  $000c(a1),d0                    move.l  $000c(a1),d0
        bpl     L00ebf4                         bpl     L00ebf4                 ; dirtyではない
        lsl.l   #8,d0                           lsl.l   #8,d0
        cmp.l   d0,d2                           cmp.l   d0,d2
        bne     L00ebf4                         bne     L00ebf4                 ; DPBが一致しない
        bsr     L00eb54                         bra     L00eb54_00ebd8          * バッファをフラッシュした後、先頭からリストを探しなおす
L00ebf4:                                L00ebf4:
        move.l  (a1),d0                         move.l  (a1),d0
        bpl     L00ebdc                         bpl     L00ebdc
        jsr     $0012(a5)                       jsr     $0012(a5)
        movem.l (a7)+,d0-d2/a1                  movem.l (a7)+,d0-d2/a1
        rts                                     rts
*/

static uint16_t iopatch2[] = {
0x6000, 0xFF5E
};


// $00ec4c-$00ec6a
// diskio_ioread処理のリエントラント対応
// バッファフラッシュ後はリンクリストが変化している可能性があるので先頭から探しなおすように変更する
/*
        変更前                                  変更後                          差分
L00ec34:                                L00ec34:
        movem.l d1-d2/a1,-(a7)                  movem.l d1-d2/a1,-(a7)
        rol.l   #8,d1                           rol.l   #8,d1
        move.b  $0000(a0),d1                    move.b  $0000(a0),d1
        ror.l   #8,d1                           ror.l   #8,d1
        add.l   d1,d2                           add.l   d1,d2
                                        L00ec42:
        move.l  $0024(a5),d0                    move.l  $0024(a5),d0
L00ec46:                                L00ec46:                                ; I/Oバッファからdirtyかつセクタ番号が指定範囲に含まれるバッファを探す
        movea.l d0,a1                           movea.l d0,a1
        tst.b   $000c(a1)                       tst.b   $000c(a1)
        bpl     L00ec5e                         bpl     L00ec5e                 * アドレスがずれたことによる飛び先変更 ; バッファはdirtyでない
        move.l  $0004(a1),d0                    move.l  $0004(a1),d0            *
        cmp.l   d2,d0                           cmp.l   d2,d0                   *
        bcc     L00ec5e                         bcc     L00ec5e                 * ; 範囲外
        cmp.l   d1,d0                           cmp.l   d1,d0                   *
        bcs     L00ec5e                         bcs     L00ec5e                 * ; 範囲外
        bsr     L00eb54                         bsr     L00eb54                 * ; dirtyなバッファをflushする ※この際にリンクリストが変化する可能性あり
                                                bra     L00ec42                 * flushによってリンクリストが変化しているので先頭から探しなおす
L00ec5e:                                L00ec5e:
        move.l  (a1),d0                         move.l  (a1),d0                 *
        bpl     L00ec46                         bpl     L00ec46                 *
        movem.l (a7)+,d1-d2/a1                  movem.l (a7)+,d1-d2/a1          *
        jmp     $0000(a5)                       jmp     (a5)                    * 短縮
*/

static uint16_t iopatch3[] = {
0x6A12, 0x2029, 0x0004, 0xB082, 0x640A, 0xB081, 0x6506, 0x6100,
0xFEF8, 0x60E2, 0x2011, 0x6AE2, 0x4CDF, 0x0206, 0x4ED5
};

//****************************************************************************
// Apply I/O patch to Human68k
//****************************************************************************

void apply_iopatch(void)
{
  if (memcmp((void *)0x00eac8, iopatch1, sizeof(iopatch1)) == 0 &&
      memcmp((void *)0x00ebf0, iopatch2, sizeof(iopatch2)) == 0 &&
      memcmp((void *)0x00ec4c, iopatch3, sizeof(iopatch3)) == 0) {
    // 既にパッチ済み
    return;
  }

  if (*(uint8_t *)0x000cbc > 0x01) {
    // 68020以上のCPUならIOCS _SYS_STATでキャッシュフラッシュする
    __asm__ volatile (
        "moveq.l #3,%%d1\n"
        "moveq.l #0xffffffac,%%d0\n"
        "trap    #15"
        : : : "d0", "d1", "memory"
    );
  }

  // Human68kにパッチを当てる
  memcpy((void *)0x00eac8, iopatch1, sizeof(iopatch1));
  memcpy((void *)0x00ebf0, iopatch2, sizeof(iopatch2));
  memcpy((void *)0x00ec4c, iopatch3, sizeof(iopatch3));

  _dos_print("Human68kのディスクI/Oドライバにパッチを適用しました\r\n");
}
