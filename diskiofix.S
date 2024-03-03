/* Human68k diskio_read fix for loopback device driver */

    .text

/*
*	diskio共通のバッファ読み込み処理
*	LRUリストの先頭からドライブ+セクタ番号が一致するものを探してあればそれを使う
*	なければリストの末尾を使う (dirtyならflushする)
*	d0のbit8=0ならディスクの内容を読み込む
*	LRUリストの先頭に繋ぐ
*/

    .globl  diskio_read_fix

diskio_read_fix:
L00eac2:
    movem.l %d1-%d3/%a2,%sp@-
    rol.l   #8,%d1
    move.b  %a0@(0x0000),%d1    // DPB - ドライブ番号
    ror.l   #8,%d1
    move.l  %a5@(0x0024),%d2    // I/OバッファのLRU管理の先頭アドレス
L00ead2:
    movea.l %d2,%a1             // ディスクI/Oバッファ
    move.l	(%a1),%d2           // 次のI/Oバッファアドレス
    bmi	    L00eaec             // 最後のバッファだった
    cmp.l   %a1@(0x0004),%d1    // 読もうとしているドライブ+セクタ番号と一致するか
    bne     L00ead2             // 次を検索
    move.l  %a1@(0x0008),%d3    // 前のI/Oバッファアドレス
    bmi     L00eb4a             // 前のバッファはない
    movea.l %d2,%a2             // 次のI/Oバッファアドレス
    move.l  %d3,%a2@(0x0008)    // 次のバッファの「前のバッファ」を更新
    bra     L00eb30
L00eaec:
    cmp.l   %a1@(0x0004),%d1    // 読もうとしているドライブ+セクタ番号と一致するか
    beq     L00eb30             // 見つかった
    tst.b   %a1@(0x0004)        // ドライブ番号
    bmi     L00eb02             // 未使用バッファ
    bclr.b  #0x07,%a1@(0x000c)  // dirty bitをクリア
    beq     L00eb02             // 元々cleanだった
    jsr     0x00eb54            // dirtyなバッファをflushする
L00eb02:
    move.l  %a0,%d3             // DPB
    rol.l   #8,%d3
    move.b  %d0,%d3
    ror.l   #8,%d3              // 上位8bitに状態フラグを設定する
    cmp.w   #0x0100,%d0
    bcc     L00eb28             // diskio_makeXXXX の場合は領域確保のみ

    // Human68kのdiskio_readではディスクI/Oバッファにセクタの内容を読み込んだ後で
    // リンクリストを繋ぎ変えるが、ループバックデバイスを使うとここが再帰的に呼ばれる
    // ために、繋ぎ変える前にリストの状態が変わってしまってリンクリストが壊れてしまう。
    // リンクリストの繋ぎ変えを先にやってからセクタ読み込みを行うことで回避する。

///////////////////////////////
    move.l  %d1,%a1@(0x0004)    // ドライブ+セクタ番号を設定
    move.l  %d3,%a1@(0x000c)    // 状態フラグ+DPBを設定
    movea.l %a1@(0x0008),%a2    // 前のI/Oバッファアドレス
    move.l  %d2,(%a2)           // 前のバッファの「次のバッファ」を更新
    movea.l %a5@(0x0024),%a2    // LRU管理の先頭I/Oバッファ
    move.l  %a1,%a2@(0x0008)    // LRU先頭バッファの「前のバッファ」を更新
    move.l  %a2,(%a1)           // 見つかったバッファの「次のバッファ」を更新
    moveq.l #-1,%d3
    move.l  %d3,%a1@(0x0008)    // 見つかったバッファの「前のバッファ」はない
    move.l  %a1,%a5@(0x0024)    // LRU管理の先頭I/Oバッファを更新
///////////////////////////////
    movem.l %d1-%d3,-(%a7)
    and.l   #0x00ffffff,%d1
    lea.l   %a1@(0x0010),%a2
    moveq.l #0x01,%d2
    jsr     %a5@(0x0000)        // 確保した領域にセクタの内容を読み込む
    movem.l (%a7)+,%d1-%d3
///////////////////////////////
    lea.l   %a1@(0x0010),%a1    // データアドレスを%a1に設定
    movem.l %sp@+,%d1-%d3/%a2
    rts

L00eb28:
    jmp     0x00eb28
L00eb30:
    jmp     0x00eb30
L00eb4a:
    jmp     0x00eb4a


    .end
