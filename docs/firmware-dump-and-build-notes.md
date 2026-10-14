# Firmware dump and build notes

この文書は、M5StickC Plus に書き込まれたESP32ファームウェアのバックアップ手順と、このプロジェクトで発生したPlatformIOのコンパイルエラーの記録です。

## フラッシュの吸い出し

### 注意

フラッシュダンプには、Wi-FiのSSID・パスワード、アプリケーションに埋め込まれた認証情報、NVSの保存データが含まれる可能性があります。ダンプをGitに追加したり、ログやチャットに貼り付けたりしないでください。

この手順で復元できるのはフラッシュ上のバイナリと保存データです。元のC/C++ソースコードやコメントを復元する手順ではありません。Flash Encryptionが有効な場合は、読み出したデータをそのまま解析できないことがあります。

### 1. USBポートを確認する

M5StickC PlusをUSBで接続し、PlatformIOのPython環境からポートを確認します。

```sh
/Users/taki/.platformio/penv/bin/python -m serial.tools.list_ports
```

macOSでは、通常 `/dev/cu.usbserial-XXXXXXXX` のようなポートがM5StickCです。`XXXXXXXX` は接続するたびに変わることがあるため、固定値として扱わないでください。

シリアルモニターやPlatformIOのMonitorがポートを使用中だと、esptoolは接続できません。先にそれらを閉じます。

### 2. esptoolの場所を設定する

この環境のPlatformIOに含まれるesptoolを使います。

```sh
PIO_PYTHON=/Users/taki/.platformio/penv/bin/python
ESPTOOL=/Users/taki/.platformio/packages/tool-esptoolpy/esptool.py
PORT=/dev/cu.usbserial-XXXXXXXX
```

PlatformIOを別の場所にインストールしている場合は、`PIO_PYTHON`と`ESPTOOL`をその環境に合わせて変更します。

### 3. フラッシュIDを確認する

読み出し前に、チップとフラッシュサイズを確認できます。

```sh
"$PIO_PYTHON" "$ESPTOOL" \
  --chip esp32 \
  --port "$PORT" \
  --baud 115200 \
  flash_id
```

今回の個体では、ESP32-PICO-D4、内蔵フラッシュ4MBとして認識されました。

### 4. 4MB全体を読み出す

バックアップ先を作り、フラッシュ全体を読み出します。

```sh
mkdir -p firmware/.pio/flash-backup

"$PIO_PYTHON" "$ESPTOOL" \
  --chip esp32 \
  --port "$PORT" \
  --baud 115200 \
  --after hard_reset \
  read_flash 0 ALL \
  firmware/.pio/flash-backup/flash.bin
```

`read_flash 0 ALL` はフラッシュの先頭から、検出したサイズ分を読み出します。処理中にM5StickCはブートローダーモードへ入り、完了後にリセットされます。

今回の個体では460800bpsだと読み出し途中で応答が途切れたため、115200bpsを使用しました。4MB全体の読み出しには数分かかります。

### 5. ダンプを保管する

読み出し後はサイズとハッシュを確認します。

```sh
ls -lh firmware/.pio/flash-backup/flash.bin
shasum -a 256 firmware/.pio/flash-backup/flash.bin
```

ダンプを別の安全な場所へコピーする場合も、認証情報を含む可能性があるファイルとして扱います。プロジェクトの `.gitignore` では `firmware/.pio` を除外しています。

### トラブルシューティング

`port is busy or doesn't exist` が出る場合は、ポート名を再確認し、シリアルモニターを閉じます。

高速通信で `No serial data received` が出る場合は、`--baud 115200` に下げて再試行します。

接続できない場合は、M5StickCのUSB接続を抜き差ししてポート名を再確認します。esptoolの接続処理中にリセットボタンを押す必要がある個体では、`Connecting...` が表示されたタイミングで一度押します。

## ダンプから認証情報を探す

ここが復元作業の中心です。4MBのダンプをいきなり全文検索するのではなく、まずESP32のパーティションテーブルを読み、アプリケーション領域とNVS領域を分けて調べます。

### 1. パーティションを特定する

ESP32のパーティションテーブルは通常 `0x8000` にあります。今回のダンプでは、次の構成でした。

| 名前 | 種類 | 開始アドレス | サイズ | 用途 |
| --- | --- | ---: | ---: | --- |
| `nvs` | data/nvs | `0x9000` | `0x5000` | Wi-Fiなどの永続設定 |
| `otadata` | data/ota | `0xE000` | `0x2000` | OTA選択情報 |
| `app0` | app/ota_0 | `0x10000` | `0x140000` | 実行ファームウェア |
| `app1` | app/ota_1 | `0x150000` | `0x140000` | OTA用のもう一方の領域 |
| `spiffs` | data/spiffs | `0x290000` | `0x160000` | ファイルシステム |

このアドレスは今回の個体の値です。別のビルドではパーティション設定により変わるため、ダンプごとにテーブルを確認します。

esptoolの `image_info` はアプリケーションバイナリ向けです。フラッシュ全体の先頭にはパーティションテーブルや空き領域があるため、4MB全体のダンプを直接 `image_info` に渡すと `invalid magic number` になります。

### 2. アプリケーション領域から文字列候補を探す

アプリ領域には、コンパイル時に埋め込まれた文字列が残ることがあります。このプロジェクトでは、BルートIDとパスワードが該当しました。

まず、既知のプロトコル文字列でアプリ領域を特定します。

```sh
DUMP=firmware/.pio/credential-recovery.j0cMdX/flash.bin

strings -a -t x "$DUMP" | rg 'SKSETPWD|SKSETRBID|SKSCAN|SKJOIN'
```

次に、値そのものを端末へ表示せず、32桁の16進文字列候補の「オフセットと長さ」だけを出します。

```sh
strings -a -t x "$DUMP" |
awk '$2 ~ /^[0-9A-Fa-f]{32}$/ { print "offset=" $1, "length=" length($2) }'
```

BルートIDは32桁の16進文字列、Bルートパスワードはこのプロジェクトでは12文字でした。候補が複数ある場合は、次の条件で絞ります。

1. 候補のフラッシュオフセットが `app0` の範囲に入っている
2. 候補の前後に、`SKSETPWD`や`SKSETRBID`などこのアプリの文字列が存在する
3. 実行コードから候補のアドレスが参照されている
4. `credentials.h` の形式と値の長さが一致する

候補の内容を確認する必要がある場合は、端末のローカル画面だけで確認し、結果をログやチャットに貼り付けません。候補を直接表示せずに調べるだけなら、次のようにします。

```sh
OFFSET=$((0x1136a))
dd if="$DUMP" bs=1 skip="$OFFSET" count=64 2>/dev/null |
  od -An -tx1 -v
```

今回のダンプでは、アプリ領域の候補は次の位置にありました。これはこのダンプ固有のオフセットであり、別のファームウェアにはそのまま適用できません。

| 候補 | ダンプ内オフセット | 確認方法 |
| --- | ---: | --- |
| BルートID | `0x1136a` | 32桁の16進文字列、アプリコードから参照 |
| Bルートパスワード | `0x1138b` | 12文字の文字列、アプリコードから参照 |
| Wi-Fiパスワード候補 | `0x11398` | アプリ文字列とNVSの`sta.pswd`を照合 |
| Wi-Fi SSID候補 | `0x113a2` | アプリ文字列とNVSの`sta.ssid`を照合 |

### 3. NVSからWi-Fi設定を探す

Wi-Fiの設定は、ESP32のNVSにも保存されます。今回のダンプでは、`nvs`領域内のnamespace index `3`に、次のキーがありました。

```text
sta.ssid
sta.pswd
```

NVSの文字列は、32バイトのエントリに分割されることがあります。そのため、単純な `strings` だけで値を読むと途中で切れたり、別のデータと連結したりします。正しく調べるには、次の順で処理します。

1. `0x9000`から`0x5000`バイトをNVS領域として切り出す
2. 4096バイト単位のページヘッダーとエントリ状態ビットマップを読む
3. namespace index `3`の`sta.ssid`と`sta.pswd`を探す
4. 文字列の`span`に従って後続の32バイトエントリを連結する
5. エントリCRCと文字列CRCの両方を検証する
6. アプリ領域から見つかったSSID・パスワード候補と完全一致するか確認する

ESP32 NVSのページ、エントリ、namespaceの構造は、[EspressifのNVS仕様](https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-reference/storage/nvs_flash.html)に記載されています。今回も、アプリ領域だけで値を決めず、CRCが有効なNVS値との一致を復元の条件にしました。

### 4. 復元ファイルを作る

4項目すべての候補を照合できたら、次の形式でGit管理外のファイルに保存します。

```cpp
#pragma once

#include <Arduino.h>

String bid = "<recovered value>";
String bpass = "<recovered value>";
const char *ssid = "<recovered value>";
const char *password = "<recovered value>";
```

保存先は `firmware/src/credentials.h` です。ファイルの権限を所有者だけに制限し、Gitに含まれていないことを確認します。

```sh
chmod 600 firmware/src/credentials.h
git check-ignore -v firmware/src/credentials.h
```

## credentials.hの復元について

このプロジェクトの現在のファームウェアでは、Wi-Fi設定とBルート設定がアプリケーションの読み取り専用データに含まれていました。また、Wi-Fi設定はESP32のNVSにも保存されていました。

そのため、完全なフラッシュダンプと実行バイナリを照合すれば、値を復元できる場合があります。復元した値はチャットやビルドログに表示せず、Git管理外の次のファイルへ保存します。

```text
firmware/src/credentials.h
```

このファイルは `.gitignore` で除外されています。ダンプから値を取り出せない場合やFlash Encryptionが有効な場合は、元の認証情報を再設定してください。

## 今回のコンパイルエラー

### 症状

`pio run -d firmware -e m5stick-c` で、次の2種類のエラーが発生していました。

```text
error: macro "min" requires 2 arguments, but only 1 given
error: 'constexpr' static data member 'min' must have an initializer
error: For RASPBERRY_PI_PICO_W board using CYW43439 WiFi only
```

### 原因

M5StickCPlus 0.0.8のeSPIヘッダーが `min` と `max` をマクロとして定義します。ESP Async WebServerが読み込むC++標準ライブラリの `std::chrono::duration::min()` と名前が衝突していました。

また、ESP Async WebServerの依存関係にRaspberry Pi Pico W用の `AsyncTCP_RP2040W` が含まれており、PlatformIOのライブラリ探索でESP32環境にもビルド対象として選ばれていました。

### 対応

`firmware/src/main.cpp` でESP Async WebServerをインクルードする前に、M5StickCPlus由来のマクロを解除しました。

```cpp
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
```

`firmware/platformio.ini` ではESP32で不要なライブラリを除外しました。

```ini
lib_ignore = AsyncTCP_RP2040W
```

### 確認結果

次のコマンドでビルド成功を確認済みです。

```sh
pio run -d firmware -e m5stick-c
```

確認時の結果は次のとおりです。

```text
RAM:   13.7% (used 45004 bytes from 327680 bytes)
Flash: 64.8% (used 849157 bytes from 1310720 bytes)
========================= [SUCCESS]
```

## `Starting...` のままに見える場合

シリアルモニターを115200bpsで開き、起動直後の状態遷移を確認します。

```sh
pio device monitor -d firmware -p /dev/cu.usbserial-XXXXXXXX -b 115200
```

今回の実機では、Wi-Fi接続後に次の順で進めば正常です。

```text
SKSCAN ...
EVENT 20 ...
EPANDESC
EVENT 22 ...
SKSREG S2 ...
SKSREG S3 ...
SKLL64 ...
SKJOIN ...
EVENT 25 ...
<instantaneous power value>
```

`EPANDESC`までは出るのに、次のスキャンを繰り返す場合は、`EVENT 22`を待つ前にスキャンのタイムアウトへ到達している可能性があります。RL7023-11では`EPANDESC`の後に`EVENT 22`が遅れて届くことがあるため、`firmware/lib/mb-rl7023-11/mb_rl7023_11.cpp`のスキャン待ち時間は余裕を持たせます。

また、PANA接続直後には電力応答ではない`ERXUDP`が届くことがあります。ECHONET Liteの`ESV=72`、`EPC=E7`、`PDC=04`を確認してから電力値を採用します。これを行わないと、起動直後だけ不正な大きな電力値を表示することがあります。

## 追加したBルートメトリクス

低圧スマート電力量メーターのGetプロパティを利用し、次の値を取得します。

| Prometheusメトリクス | EPC | 内容 | 取得間隔 |
| --- | --- | --- | ---: |
| `instantaneous_power` | `0xE7` | 瞬時電力、W | 要求の最小間隔1秒（実機で通常約2〜2.4秒） |
| `instantaneous_current_amps{phase="r"}` | `0xE8` | 瞬時電流R相、A | 30秒 |
| `instantaneous_current_amps{phase="t"}` | `0xE8` | 瞬時電流T相、A | 30秒 |
| `cumulative_energy_import_kwh` | `0xE0` | 積算電力量・正方向、kWh | 30分 |
| `cumulative_energy_export_kwh` | `0xE3` | 積算電力量・逆方向、kWh | 30分 |
| `cumulative_energy_unit_kwh` | `0xE1` | 生値1カウントあたりの単位、kWh | 起動時・再試行 |

`E3`は逆方向計測に対応していないメーターでは未提供値になるため、その場合はメトリクスを出しません。`E1`の単位コードを使って、メーターから返る4バイト値をkWhへ換算しています。

今回の実機では、`/metrics`から瞬時電力、R/T相電流、正方向積算電力量、逆方向積算電力量のすべてを確認しました。

### メトリクスの有効・無効

取得と公開を個別に止めたい場合は、`firmware/src/metrics_config.h`を編集して、対象の`kEnable...`を`false`にします。

```cpp
constexpr bool kEnableInstantaneousPower = true;
constexpr bool kEnableInstantaneousCurrent = true;
constexpr bool kEnableCumulativeEnergyImport = true;
constexpr bool kEnableCumulativeEnergyExport = true;
```

無効にしたメトリクスは、Bルートへの要求もPrometheusの`/metrics`への出力も行いません。
設定変更後は再ビルド・書き込みが必要です。

### 電力の取得周期を短縮した実機検証（2026-10-06〜07）

E7単独・同時要求1件の状態で、要求開始間隔を5秒→2秒→1秒と短縮しました。
これはメーター内部の計測周期ではなく、Bルート経由の取得周期の検証です。

| 要求間隔の設定 | 観測結果 |
| --- | --- |
| 5秒 | 取得周期は約5秒。ログで対応付けた6応答の往復時間は2.108〜2.510秒。値の変化も確認。 |
| 2秒 | 通常の往復時間が設定間隔を超え、応答後すぐに次を送る状態。接続直後の初回要求は60秒タイムアウト、再要求後は約2.1〜2.4秒で応答。 |
| 1秒 | 90秒のシリアル観測中、接続後にE7が30件応答。初回は約3.27秒、その後は約2〜2.4秒。観測中の要求タイムアウトなし。 |

1秒設定ですでに応答直後に次を送るため、さらに設定値を短くしても、この観測条件では速くなりません。
既定値を1秒にして、応答待ち中は要求を重ねず、通信が許す速度で取得します。
電流・積算値が取得期限に達した場合は先に処理するため、その前後の電力更新間隔は長くなります。
短時間・単一実機の結果であり、絶対的な最小周期や長時間の無欠測は保証していません。
また、同じ電力値が続いても負荷が一定の可能性があり、値の一致だけから内部計測周期は推定できません。

高速化に伴い、`loop()`では受信値を表示・公開用変数へ取り込んでから次の要求を開始する順序に変更しました。
以前の順序では、応答直後の再要求が未読フラグを消し、E7の通信は速くても表示・`/metrics`が更新されない場合がありました。
Prometheusなどの収集側でこの速度を利用する場合は、そちらのスクレイプ間隔も確認してください。

修正版を書き込んだ後、全メトリクス有効で150秒間シリアルを観測しました。
E7応答48件に対して表示・公開用変数への反映も48件で、受信値の取り落としはありませんでした。
電力更新間隔は最小1.996秒、中央値2.400秒、最大7.900秒（電流・積算値の取得や通信遅延を含む）。
電流は約30秒ごとに繰り返し更新され、起動時の正逆積算値も取得できました。
HTTP `/metrics`でも電力値の変化（361W→339W）と他メトリクスの公開を確認済みです。

### 7.9秒の空白の追加調査

追加の実測で、電力単独の要求でも送信完了後の応答待ちが約7.5秒かかるケースを再現しました。
電流の期限時はE7+E8を同じ要求にまとめ、積算値は電力の間に1件ずつ取得する構成へ変更しています。
比較結果、遅延の内訳、再現手順は[追加調査記録](polling-latency-2026-10-07.md)を参照してください。
`kEnablePowerTimingMetrics`で受信値の経過秒数・更新間隔・受信件数を公開できます。
