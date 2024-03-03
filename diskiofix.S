/// Human68k diskio service fix patches for loopback device driver ///

    .text

    .globl  diskio_read_fix
    .globl  diskio_flush_fix
    .globl  diskio_ioread_fix

///
//  diskio共通のバッファ読み込み処理
//  LRUリストの先頭からドライブ+セクタ番号が一致するものを探してあればそれを使う
//  なければリストの末尾を使う (dirtyならflushする)
//  d0のbit8=0ならディスクの内容を読み込む
//  LRUリストの先頭に繋ぐ
///

diskio_read_fix:
L00eac2:
    movem.l %d1-%d3/%a2,%sp@-
    rol.l   #8,%d1
    move.b  %a0@(0x0000),%d1    // DPB - ドライブ番号
    ror.l   #8,%d1
L00eace:
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
    bsr     L00eb54             // dirtyなバッファをflushする
    bra     L00eace                 // # flushの際に再帰呼び出しされる可能性がある
                                    // # リンクリストが繋ぎ変えられている場合があるので
                                    // # 再度先頭からリストを追い直す。
L00eb02:
    move.l  %a0,%d3             // DPB
    rol.l   #8,%d3
    move.b  %d0,%d3
    ror.l   #8,%d3              // 上位8bitに状態フラグを設定する
    cmp.w   #0x0100,%d0
    bcc     L00eb28             // diskio_makeXXXX の場合は領域確保のみ

    // # Human68kのdiskio_readではディスクI/Oバッファにセクタの内容を読み込んだ後で
    // # リンクリストを繋ぎ変えるが、ループバックデバイスを使うとここが再帰的に呼ばれる
    // # ために、繋ぎ変える前にリストの状態が変わってしまってリンクリストが壊れてしまう。
    // # リンクリストの繋ぎ変えを先にやってからセクタ読み込みを行うことで回避する。

    // リンクリストの繋ぎ変え
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
    // セクタ読み込み
    movem.l %d1-%d3,%a7@-
    and.l   #0x00ffffff,%d1
    lea.l   %a1@(0x0010),%a2
    moveq.l #0x01,%d2
    jsr     %a5@(0x0000)        // 確保した領域にセクタの内容を読み込む
    movem.l %a7@+,%d1-%d3

L00eb4a:
    lea.l   %a1@(0x0010),%a1    // データアドレスを%a1に設定
    movem.l %sp@+,%d1-%d3/%a2
    rts

///
//  diskio_flush
//  I/Oバッファの全セクタについて、指定のDPBでdirtyなものをflushする
///

diskio_flush_fix:
L00ebd0:
    movem.l %d0-%d2/%a1,%a7@-
    move.l  %a0,%d2             // DPB
    lsl.l   #8,%d2
L00ebd8:
    move.l  %a5@(0x0024),%d0    // I/OバッファのLRU管理の先頭アドレス
L00ebdc:
    movea.l %d0,%a1
    tst.b   %a1@(0x0004)        // ドライブ番号
    bmi     L00ebf4	            // バッファは未使用
    move.l  %a1@(0x000c),%d0    // DPB
    bpl     L00ebf4	            // cleanなら何もしない
    lsl.l   #8,%d0
    cmp.l   %d0,%d2
    bne     L00ebf4             // DPBが異なる
    bsr     L00eb54             // 同じDPBなので書き込む
    bra     L00ebd8                 // # flushの際に再帰呼び出しされる可能性がある
                                    // # リンクリストが繋ぎ変えられている場合があるので
                                    // # 再度先頭からリストを追い直す。
L00ebf4:
    move.l  (%a1),%d0           // 次のI/Oバッファアドレス
    bpl     L00ebdc
    jsr     %a5@(0x0012)        // メディア排出許可処理を呼ぶ
    movem.l	%a7@+,%d0-%d2/%a1
    rts

///
//  diskio_ioread
//  複数セクタの読み込みを行う
//  I/Oバッファに範囲内でdirtyなデータがあった場合は事前にflushする
///

diskio_ioread_fix:
L00ec34:
    movem.l %d1-%d2/%a1,%a7@-
    rol.l   #8,%d1
    move.b  %a0@(0x0000),%d1    // DPB - ドライブ番号
    ror.l   #8,%d1
    add.l   %d1,%d2             // 最終セクタ番号+1
L00ec42:
    move.l  %a5@(0x0024),%d0    // I/OバッファのLRU管理の先頭アドレス
L00ec46:
    movea.l %d0,%a1
    tst.b   %a1@(0x000c)        // 状態フラグ
    bpl     L00ec5e             // cleanバッファ
    move.l  %a1@(0x0004),%d0    // ドライブ+セクタ番号
    cmp.l   %d2,%d0
    bcc     L00ec5e             // 範囲外
    cmp.l   %d1,%d0
    bcs     L00ec5e             // 範囲外
    bsr     L00eb54             // dirtyなのでflushする
    bra     L00ec42                 // # flushの際に再帰呼び出しされる可能性がある
                                    // # リンクリストが繋ぎ変えられている場合があるので
                                    // # 再度先頭からリストを追い直す。
L00ec5e:
    move.l  (%a1),%d0           // 次のI/Oバッファ
    bpl     L00ec46
    movem.l %a7@+,%d1-%d2/%a1
    jmp     %a5@(0x0000)        // すべてflushし終えたらreadする


//  Human68k本体内の処理にジャンプする

L00eb28:
    jmp     0x00eb28            // バッファをLRU管理の先頭に繋ぎ変える (diskio_makeXXXXで使用)
L00eb30:
    jmp     0x00eb30            // バッファをLRU管理の先頭に繋ぎ変える (キャッシュhitの場合)
L00eb54:
    jmp     0x00eb54            // a1の指すI/Oバッファの内容をフラッシュする (diskio_writeを呼ぶ)

    .end
