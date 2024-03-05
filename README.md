X680x0 ループバックデバイスドライバ loopdrv
=========================================

## 概要

ループバックデバイスは、ファイルシステム上にある通常のファイルをディスクドライブのようなブロックデバイスに見せかけるための仕組みです。
Linux などの OS がこの機能をサポートしていますが、本ドライバはこの機能を X680x0 の Human68k 上で実現するためのものです。

# 使用方法

本ドライバは、Human68k に組み込むドライバ本体 loopdrv.sys と、ループバックデバイスを操作するためのコマンド losetup.x からなります。

## デバイスドライバのインストール

まず、CONFIG.SYS に以下の記述を追加して loopdrv.sys を組み込みます。

```
DEVICE = LOOPDRV.SYS [/u<ユニット数>] [/r]
```
* `/u<ユニット数>` には、ループバックデバイスのために予約するドライブのユニット数を 1～8 の値で指定します。省略した場合はユニット数 1 となります。
* `/r` は、ループバックデバイスのマウント時のデフォルトを読み込み専用にします。
(読み込み専用/読み書き可能はlosetupコマンドで切り替えられます)。

loopdrv.sys を組み込んで起動すると、以下のメッセージが出てループバックデバイスが利用できるようになります。

```
Loopback device driver for X680x0 version xxxxxxxx
ドライブX:でループバックデバイスが利用可能です
```

## losetup コマンドによるループバックデバイスの操作

ループバックデバイスの操作は losetup コマンドで行います。

* losetup
  * ループバックデバイスの現在の設定状態を表示します。
* losetup -h
  * コマンドの使い方を表示します。
* losetup -D
  * マウントされているすべてのループバックデバイスをアンマウントします。
* losetup -d <ドライブ名>
  * `<ドライブ名>` で指定したループバックデバイスをアンマウントします。
* losetup [-r][-w] <ドライブ名>
  * `<ドライブ名>` で指定したループバックデバイスの状態を変更します。
    * `-r` : ドライブを読み込み専用にします。
    * `-w` : ドライブを読み書き可能にします。
* losetup [-r][-w] [<ドライブ名>] <イメージファイル名>
  * `<ドライブ名>` で指定したループバックデバイスに `<イメージファイル名>` のファイルをマウントします。
    * `-r` : 読み込み専用でマウントします。
    * `-w` : 読み書き可能でマウントします。
    * `<ドライブ名>` を省略した場合は、使われていないループバックドライブのうち最初のものが使用されます。
  * イメージファイルのヘッダやファイルサイズ等から判断して、以下のいずれかのデバイスとしてマウントします。
    * フロッピーディスク
      * 2HD (1232kB)
      * 2HC (1200kB)
      * 2HQ (1440kB)
      * 2DD (640kB)
      * 2DD (720kB)
      * イメージファイルはベタイメージ(XDFファイル)、DIMファイル、D88/D68ファイルに対応しています。
    * SASIハードディスク (HDFファイル)
    * SCSIハードディスク (HDSファイル)/MOディスク (MOSファイル)

# 制約と注意事項

* Human68k v3.02 専用です。他のバージョンでは使用できません。
* FASTIO.X のような、Human68kのディスクI/Oバッファ処理を変更するドライバとは共存できません。
  * loopdrv.sys は自分自身以外のドライバがバッファI/O処理を差し替えていることを検出すると、安全のためすべてのループバックデバイスへの操作を無効にします。
* ハードディスクのイメージファイルに複数のパーティションが切られている場合、最初のパーティションのみがマウントされます。
* ループバックマウントしたドライブをフォーマットする際は、FORMAT.X に必ず `/C` オプションを付けて論理フォーマットのみを行うようにしてください。
  * FORMAT.X は、メディアバイトがフロッピーディスクであるドライブに対して通常のフォーマットを行うと、そのドライブのユニット番号と同じ番号のディスクドライブに対して物理フォーマットを行ってしまうようです。
  * つまり、LOOPDRV.SYS で用意された最初のドライブ(ユニット番号0)をフォーマットすると、フロッピーディスクドライブ0 が物理フォーマットされてしまうということです。たまたまその時ドライブにディスクが入っていると、そのままディスクが物理フォーマットされてしまいます。
  * 事故を防ぐため、ループバックデバイスに対してのフォーマット操作はできるだけ避けて、エミュレータ等で作成したフォーマット済みのイメージファイルを使用することをお勧めします。

# 謝辞

Human68k のディスクI/Oバッファの仕組みについては、ぷにぐらま～ずマニュアル https://github.com/kg68k/puni (立花@桑島技研 さん作) の [fs_ospatch.txt](https://github.com/kg68k/puni/blob/main/fs_ospatch.txt) が参考になりました。感謝します。
