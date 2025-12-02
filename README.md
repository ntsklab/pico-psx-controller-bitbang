# PSX Controller Bit-Banging Simulator

Raspberry Pi Pico (RP2040) を使用した、ビットバンギングによるPS1/PS2コントローラシミュレータです。

※ばいぶこーでぃんぐってやつです。ちがうかも。すげー。変なところ多いかもだけど動けばヨシとされます。

## 特徴

- ✅ **デュアルコア構成** - Core0でボタンポーリング、Core1でPSX通信
- ✅ **デジタルコントローラモード** - 14ボタン対応
- ✅ **ACK Auto-Tuning** - PS1/PS2両対応の自動タイミング調整
- ✅ **1kHz高精度ボタン読み取り**
- ✅ **ボタンラッチングモード** - 1フレーム未満の短い入力も検出可能
- ✅ **SOCD Cleaner**
- ✅ **メモリカード共存** - PS1でメモリカードと併用可能
- ✅ **統計機能** - PSXポーリングレート、ボタンサンプリングレートの計測

## ハードウェア要件

### 使用マイコン
- Raspberry Pi Pico (RP2040)
- システムクロック: 125MHz

### PSXバス信号接続

| 信号 | GPIO | 方向 | 説明 |
|------|------|------|------|
| DAT  | 3    | I/O  | データライン (オープンドレイン) |
| CMD  | 4    | IN   | コマンドライン |
| SEL  | 10   | IN   | セレクト (アクティブLOW) |
| CLK  | 6    | IN   | クロック (~250kHz) |
| ACK  | 7    | OUT  | アクノリッジ (オープンドレイン) |

### ボタン入力GPIO

| ボタン | GPIO | ボタン | GPIO |
|--------|------|--------|------|
| ○      | 22   | △      | 20   |
| ×      | 21   | □      | 19   |
| L1     | 14   | R1     | 12   |
| L2     | 13   | R2     | 11   |
| UP     | 18   | DOWN   | 17   |
| LEFT   | 16   | RIGHT  | 15   |
| START  | 26   | SELECT | 27   |

### 状態表示LED
- GPIO 25 (Pico内蔵LED)

## ビルド方法

### 前提条件
- Raspberry Pi Pico C/C++ SDK 2.2.0以上
- CMake 3.13以上
- arm-none-eabi-gcc コンパイラ
- または VS Code + Raspberry Pi Pico 拡張機能

### ビルド手順

#### コマンドライン
```bash
# プロジェクトディレクトリに移動
cd pico-psx-controller-bitbang

# ビルドディレクトリが既に存在する場合は再生成
rm -rf build
mkdir build
cd build

# CMake設定
cmake ..

# ビルド実行
make -j4
# または ninja を使用
ninja
```

#### VS Code
1. Raspberry Pi Pico 拡張機能をインストール
2. プロジェクトを開く
3. タスク: "Compile Project" を実行 (Ctrl+Shift+B)
4. `build/pico-psx-controller-bitbang.uf2` が生成される

## 設定

### config.h で変更可能な設定

#### ACK Auto-Tuning
```c
// 1: 有効（デフォルト、PS1/PS2自動対応）
// 0: 無効（固定タイミング使用）
#define ACK_AUTO_TUNE_ENABLED 1
```

Auto-Tuning有効時、起動時に自動的に最適なACKタイミングを検出します。手元のコンソールでは以下で動作するようです。：
- PS2: 短いパルス幅（1-2µs）で動作
- PS1: 長めのパルス幅（3-6µs）で動作
- 検出完了後はLOCKEDとなり、タイミング固定

尚、手元のPS2はPS1のパルス幅でも動作するようなのでこの機能を使わなくても良いのですが、コンソールのリビジョンによって異なる動作になると嫌なのでデフォルト有効です。

#### ボタン入力モード
```c
// 0: Direct mode - PSXポーリング時の状態を読み取る。１フレーム未満の入力は取りこぼす場合有り。恐らく通常のコントローラの仕様はこちら？（デフォルト）
// 1: Latching mode - ボタンが押されたらPSXが読むまで保持。1フレーム未満の入力も検出可能。
#define BUTTON_LATCHING_MODE    0
```

シリアルコンソールからlatchコマンドで動的に変更可能です。また、saveコマンドでFlashへ設定が保存できます。

#### ボタンサンプリングレート
```c
// 1000µs = 1kHz (デフォルト)
#define BUTTON_POLL_INTERVAL_US 1000
```

#### デフォルトデバッグモード
```c
// 1: 起動時デバッグON
// 0: 起動時デバッグOFF（デフォルト）
#define DEBUG_ENABLED 0
```

シリアルコンソールからdebugコマンドで動的に変更可能です。また、saveコマンドでFlashへ設定が保存できます。

## 使用方法

### シリアルコマンド

シリアル（115200bps）から以下のコマンドが使用可能：

| コマンド | 説明 |
|---------|------|
| `debug` | デバッグモードON/OFF切り替え |
| `latch` | ラッチングモードON/OFF切り替え |
| `save` | 現在の設定をFlashに保存 |
| `help` または `?` | コマンド一覧と現在の設定を表示 |

**設定の永続化**: `save`コマンドで設定を保存すると、次回起動時に自動的に読み込まれます。

### LED表示

#### デバッグモードON時
| 状態 | 説明 |
|------|------|
| 消灯 | アイドル（PSXポーリングなし） |
| 点灯 | トランザクション処理中 |

#### デバッグモードOFF時
| パターン | 状態 | 説明 |
|---------|------|------|
| 1回点滅 | READY | 準備完了（待機中） |
| 2回点滅 | POLLING | アクティブにポーリング中（ラッチングOFF） |
| 3回点滅 | POLLING | アクティブにポーリング中（ラッチングON） |
| 高速点滅 | ERROR | エラー発生（100ms周期） |

**点滅パターン**: 100ms点灯 + 200ms消灯の繰り返し、その後700ms休止

### デバッグ出力

デバッグモードON時、USB CDC経由でデバッグ情報が2秒ごとに出力されます。

シリアルモニタ (115200bps) で以下の情報を確認可能:
- **トランザクション統計**: 総数、コントローラー、メモリカード、無効、タイムアウト
- **ACK Auto-Tuning状態**: waiting.../tuning.../LOCKED、ACKパルス幅とウェイト時間
- **PSXポーリング間隔**: 最小/最大/平均値、ポーリングレート(Hz)
- **ボタンサンプリング**: 目標レート、実測間隔、実測レート
- **ボタン状態**: 16進数表記と押下ボタンリスト

Auto-Tuningの進行状況:
```
[ACK-TUNE] Starting auto-tune...
[ACK-TUNE] New best: PULSE=3, WAIT=1 (87.5%, 7/8)
[ACK-TUNE] LOCKED: PULSE=3 us, WAIT=1 us (88%)
```

**注意**: デバッグモードON時はprintf処理によりボタンポーリング間隔のばらつきが発生します。本番使用時はデバッグOFFを推奨します。

## アーキテクチャ

### デュアルコア構成

#### Core 0 (メインループ)
- ボタン状態のポーリング (1kHz)
- 共有メモリへのボタンデータ書き込み
- LED状態管理
- デバッグ出力

#### Core 1 (PSX通信専用)
- SELECT信号の監視
- CLKエッジ同期によるバイト送受信
- コマンド解析とレスポンス生成
- ACKパルス生成

### モジュール構成

```
src/
├── main.c              Core0メインループと初期化
├── psx_protocol.c/h    PSXプロトコル層（Core1）
├── psx_bitbang.c/h     ビットバンギング低レベル関数
├── button_input.c/h    ボタン入力処理
├── shared_state.c/h    コア間データ共有
└── config.h            設定定数とピン定義
```

## トラブルシューティング

### コントローラが認識されない

1. **配線確認**
   - 各ピンの接続を確認

2. **信号確認**
   - オシロスコープでCLK信号を確認
   - SELECT信号がLOWになっているか確認

3. **デバッグ出力**
   - `DEBUG_ENABLED` を有効化
   - トランザクション統計を確認

## 参考資料

- [PSX-SPX Documentation](https://psx-spx.consoledev.net/controllersandmemorycards/)
- [Raspberry Pi Pico Datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Pico C/C++ SDK Documentation](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf)
- [PicoGamepadConverter](https://github.com/Loc15/PicoGamepadConverter)
- [PicoMemcard](https://github.com/dangiu/PicoMemcard/blob/pmc%2B/release/poc_examples/controller_simulator/controller_simulator.c)
- [GameSX PSX Controller Data](https://gamesx.com/controldata/psxcont/psxcont.htm)

---

**注意**: このプロジェクトは教育・趣味目的です。
