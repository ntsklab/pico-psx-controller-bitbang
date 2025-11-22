# PSX Controller Bit-Banging Simulator

Raspberry Pi Pico (RP2040) を使用した、 **ビットバンギング（GPIO直接制御）** によるPS1/PS2コントローラシミュレータです。

## 特徴

- ✅ **デュアルコア構成** - Core0でボタンポーリング、Core1でPSX通信
- ✅ **デジタルコントローラモード** - 14ボタン対応
- ✅ **ACK Auto-Tuning** - PS1/PS2両対応の自動タイミング調整
- ✅ **時刻ベースサンプリング** - 1kHz高精度ボタン読み取り
- ✅ **ボタンラッチングモード** - 1フレーム未満の短い入力も検出可能
- ✅ **SOCD Cleaner** - HitBox型格闘スティック対応（反対方向同時押し=ニュートラル）
- ✅ **メモリカード共存** - 同一ポートでメモリカードと併用可能
- ✅ **統計機能** - PSXポーリングレート、ボタンサンプリングレートの計測
- ✅ **ランタイムデバッグ切り替え** - シリアルコマンドで動的ON/OFF

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

**全てのボタンはアクティブLOW（押下時=LOW）**

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

### Picoへの書き込み

#### UF2ファイル経由
1. PicoのBOOTSELボタンを押しながらUSB接続
2. マスストレージデバイスとして認識される
3. `build/pico-psx-controller-bitbang.uf2` をドラッグ&ドロップ
4. 自動的に再起動して実行開始

## 設定

### config.h で変更可能な設定

#### ACK Auto-Tuning
```c
// 1: 有効（デフォルト、PS1/PS2自動対応）
// 0: 無効（固定タイミング使用）
#define ACK_AUTO_TUNE_ENABLED 1
```

Auto-Tuning有効時、起動時に自動的に最適なACKタイミングを検出します：
- PS2: 短いパルス幅（1-2µs）で高速動作
- PS1: 長めのパルス幅（3-6µs）で安定動作
- 検出完了後はLOCKEDとなり、タイミング固定

#### ボタン入力モード
```c
// 0: Direct mode - PSXポーリング時の状態を読み取る（デフォルト）
// 1: Latching mode - ボタンが押されたらPSXが読むまで保持（1フレーム未満の入力も検出）
#define BUTTON_LATCHING_MODE    0
```

#### ボタンサンプリングレート
```c
// 1000µs = 1kHz (デフォルト)
#define BUTTON_POLL_INTERVAL_US 1000
```

#### デフォルトデバッグモード
```c
// 1: 起動時デバッグON
// 0: 起動時デバッグOFF（デフォルト、実行時切り替え可能）
#define DEBUG_ENABLED 0
```

## 使用方法

### 基本動作

1. Picoに電源を供給
2. PSXコンソールまたはアダプタのコントローラポートに接続
3. ボタンを押すと即座にPSXに反映される

### デバッグモード切り替え

シリアルモニタ（115200bps）から動的にデバッグモードを切り替え可能：

```
debug [Enter]
```

- **デバッグON**: 統計情報を2秒ごとに表示、LED動作シンプル
- **デバッグOFF**: 統計非表示、LED点滅パターン動作

起動時のメッセージで現在のモードを確認できます。

### 推奨設定

- **通常プレイ**: `BUTTON_LATCHING_MODE 0`, デバッグOFF
- **格闘ゲーム**: `BUTTON_LATCHING_MODE 1`, デバッグOFF（短い入力も確実に検出）
- **開発/デバッグ**: デバッグON（統計情報でタイミング確認）

### LED表示

#### デバッグモードON時
| 状態 | 説明 |
|------|------|
| 消灯 | アイドル（PSXポーリングなし） |
| 点灯 | トランザクション処理中 |

#### デバッグモードOFF時
| 状態 | 説明 |
|------|------|
| 1回点滅 | 準備完了（READY） |
| 2回点滅 | アクティブにポーリング中（POLLING） |
| 3回点滅 | エラー発生（ERROR） |

### デバッグ出力

デバッグモードON時、USB CDC経由でデバッグ情報が2秒ごとに出力されます。

シリアルモニタ (115200bps) で以下の情報を確認可能:
- **トランザクション統計**: 総数、コントローラー、メモリカード、無効、タイムアウト
- **ACK Auto-Tuning状態**: waiting.../tuning.../LOCKED、パルス幅とウェイト時間
- **PSXポーリング間隔**: 最小/最大/平均値、ポーリングレート(Hz)
- **ボタンサンプリング**: 目標レート、実測間隔、実測レート
- **ボタン状態**: 16進数表記と押下ボタンリスト

Auto-Tuningの進行状況:
```
[ACK-TUNE] Starting auto-tune...
[ACK-TUNE] New best: PULSE=3, WAIT=1 (87.5%, 7/8)
[ACK-TUNE] LOCKED: PULSE=3 us, WAIT=1 us (88%)
```

**注意**: デバッグモードON時はprintf処理により若干のタイミングばらつきが発生します。本番使用時はデバッグOFFを推奨します。

## アーキテクチャ

### デュアルコア構成

#### Core 0 (メインループ)
- ボタン状態の高速ポーリング (1kHz)
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
   - GNDが共通接続されているか確認

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
