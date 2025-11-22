# PSX Controller Bit-Banging Simulator

Raspberry Pi Pico (RP2040) を使用した、 **ビットバンギング（GPIO直接制御）** によるPS1/PS2コントローラシミュレータです。

## 特徴

- ✅ **デュアルコア構成** - Core0でボタンポーリング、Core1でPSX通信
- ✅ **デジタルコントローラモード** - 14ボタン対応

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
- Raspberry Pi Pico C/C++ SDK インストール済み
- CMake 3.13以上
- arm-none-eabi-gcc コンパイラ

### ビルド手順

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
```

### Picoへの書き込み

1. PicoのBOOTSELボタンを押しながらUSB接続
2. マスストレージデバイスとして認識される
3. `pico-psx-controller-bitbang.uf2` をドラッグ&ドロップ
4. 自動的に再起動して実行開始

## 使用方法

### 基本動作

1. Picoに電源を供給
2. PSXコンソールまたはアダプタのコントローラポートに接続
3. ボタンを押すと即座にPSXに反映される

### LED表示

| 状態 | 説明 |
|------|------|
| 消灯 | アイドル（PSX未接続またはポーリングなし） |
| 点灯 | トランザクション処理中 |
| 高速点滅 | エラー発生 |
| 低速点滅 | メモリーカードアクセス検出 |

### デバッグ出力

`src/config.h` の `DEBUG_ENABLED` を `1` に設定すると、USB CDC経由でデバッグ情報が出力されます。

```c
#define DEBUG_ENABLED 1
```

シリアルモニタ (115200bps) で以下の情報を確認可能:
- トランザクション統計
- ボタン状態
- エラー情報

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

---

**注意**: このプロジェクトは教育・趣味目的です。
