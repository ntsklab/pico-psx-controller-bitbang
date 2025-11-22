# PSX Controller Bit-Banging Simulator

Raspberry Pi Pico (RP2040) を使用した、**ビットバンギング（GPIO直接制御）**によるPS1/PS2コントローラシミュレータです。

## 特徴

- ✅ **完全なビットバンギング実装** - PIOを使わずGPIO直接制御
- ✅ **デュアルコア構成** - Core0でボタンポーリング、Core1でPSX通信
- ✅ **メモリーカード共存対応** - オープンドレイン制御による完全なバス共有
- ✅ **正確なタイミング制御** - CLKエッジ同期とACKパルス生成
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

生成されるファイル:
- `pico-psx-controller-bitbang.uf2` - Picoへの書き込み用
- `pico-psx-controller-bitbang.elf` - デバッグ用

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

## 技術詳細

### ビットバンギング実装

CLKエッジ検出とデータ送受信を完全にソフトウェアで実装:

```c
// CLK立ち上がりエッジでCMDビットを読み取り
for (int bit = 0; bit < 8; bit++) {
    wait_clk_rising();
    if (gpio_get(PIN_CMD)) data |= (1 << bit);
}

// CLK立ち下がりエッジでDATビットを出力
for (int bit = 0; bit < 8; bit++) {
    wait_clk_falling();
    if (data & (1 << bit)) {
        gpio_set_dir(PIN_DAT, GPIO_IN);   // Hi-Z
    } else {
        gpio_set_dir(PIN_DAT, GPIO_OUT);  // LOW
    }
}
```

### オープンドレイン制御

DATとACKラインはオープンドレイン動作:

- **Hi-Z状態**: `gpio_set_dir(pin, GPIO_IN)` - 外部プルアップで HIGH
- **LOW出力**: `gpio_set_dir(pin, GPIO_OUT)` + `gpio_put(pin, 0)`

この方式により、メモリーカードと安全にバス共有が可能。

### コア間通信

ダブルバッファリングによるロックフリーなデータ交換:

```c
// Core 0: 書き込み
uint32_t write_idx = 1 - g_shared_state.read_index;
g_shared_state.buffer[write_idx] = new_data;
g_shared_state.write_index = write_idx;

// Core 1: 読み取り
uint32_t read_idx = g_shared_state.write_index;
g_shared_state.read_index = read_idx;
data = g_shared_state.buffer[read_idx];
```

### SELECT割り込み

SELECT立ち上がりエッジで即座にバス解放:

```c
void psx_sel_interrupt_handler(void) {
    gpio_set_dir(PIN_DAT, GPIO_IN);  // Hi-Z
    gpio_set_dir(PIN_ACK, GPIO_IN);  // Hi-Z
    transaction_active = false;
}
```

## トラブルシューティング

### コントローラが認識されない

1. **配線確認**
   - 各ピンの接続を確認
   - GNDが共通接続されているか確認
   - プルアップ抵抗（1kΩ程度）がDATとACKに接続されているか

2. **信号確認**
   - オシロスコープでCLK信号を確認（~250kHz）
   - SELECT信号がLOWになっているか確認

3. **デバッグ出力**
   - `DEBUG_ENABLED` を有効化
   - トランザクション統計を確認

### ボタンが反応しない

1. ボタンGPIOの配線確認
2. プルアップ抵抗の確認（内部プルアップ有効）
3. デバッグ出力でボタン値を確認

### メモリーカードが動作しない

1. SELECT非選択時にDATとACKがHi-Zになっているか確認
2. メモリーカードアドレス（0x81）で応答していないか確認
3. オシロスコープでバス競合がないか確認

## ライセンス

このプロジェクトはMITライセンスの下で公開されています。

## 参考資料

- [PSX-SPX Documentation](https://psx-spx.consoledev.net/controllersandmemorycards/)
- [Raspberry Pi Pico Datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Pico C/C++ SDK Documentation](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf)

## 貢献

バグレポート、機能要望、プルリクエストを歓迎します。

## 作者

PSX Controller Bit-Banging Implementation Project

---

**注意**: このプロジェクトは教育・趣味目的です。商用利用する場合は、関連する特許やライセンスを確認してください。
