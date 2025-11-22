# PSX Controller Simulator - Bit-Banging Implementation Project

## プロジェクト概要

Raspberry Pi Pico（RP2040）を使用して、PIOを使わずに**純粋なビットバンギング（GPIO直接制御）**でPS1/PS2コントローラをシミュレートするプロジェクトを作成してください。

現在のPIO実装では、メモリーカードと同一バス上での動作時に問題が発生しています。ビットバンギング実装により、より細かいタイミング制御とバス制御を実現し、メモリーカードとの共存を可能にします。

---

## ハードウェア仕様

### 使用マイコン
- **Raspberry Pi Pico (RP2040)**
- システムクロック: 125MHz
- デュアルコア (Cortex-M0+)

### PSX/PS2 バス信号ピン配置（変更不可）

```
PIN_DAT = GPIO 3   // Data line (Open-drain, bidirectional)
PIN_CMD = GPIO 4   // Command line (Input from PSX)
PIN_SEL = GPIO 10  // Select/Chip Select (Input from PSX, Active LOW)
PIN_CLK = GPIO 6   // Clock (Input from PSX, ~250kHz)
PIN_ACK = GPIO 7   // Acknowledge (Open-drain output to PSX)
```

### ボタン入力GPIOピン配置（変更不可）

```c
// Face buttons
GPIO 22 = Circle (○)
GPIO 21 = Cross (×)
GPIO 20 = Triangle (△)
GPIO 19 = Square (□)

// Shoulder buttons
GPIO 14 = L1
GPIO 12 = R1
GPIO 13 = L2
GPIO 11 = R2

// D-pad
GPIO 18 = UP
GPIO 17 = DOWN
GPIO 16 = LEFT
GPIO 15 = RIGHT

// System buttons
GPIO 26 = START
GPIO 27 = SELECT
```

**ボタンは全てアクティブロー（押下時 = LOW）で配線されています。**

### 状態表示LED
- **PICO_DEFAULT_LED_PIN** (GPIO 25) - Picoボード上のLED

---

## PSX/PS2 通信プロトコル仕様

### 基本タイミング

- **ボーレート**: 約250kHz (標準)
- **クロック極性**: アイドル時HIGH (CPOL=1)
- **クロックフェーズ**: 立ち下がりエッジでデータ出力、立ち上がりエッジでサンプル (CPHA=1)
- **ビット順序**: LSB first
- **ACKパルス幅**: 最低2μs
- **ACKタイミング**: 各バイト転送後、最後のクロックパルスから2-3μs以内
- **ACKタイムアウト**: PSXは約100μs以内にACKを期待

### デバイスアドレッシング

PSXは最初のバイトでデバイスを選択します：
- `0x01` = 標準コントローラ
- `0x81` = メモリーカード

### 信号線の特性

#### オープンドレイン信号 (DAT, ACK)
- **Hi-Z状態**: ピンを入力モードに設定 → 外部プルアップによりHIGH
- **LOW出力**: ピンを出力モードに設定し、LOWを出力
- 複数デバイスがバスを共有可能（非選択時は必ずHi-Z）

#### 重要な制約
- **SELECT がHIGHのとき、DATとACKは絶対にHi-Z状態を維持すること**
- メモリーカードがアクセスされている間、コントローラはバスに干渉してはいけない

### コントローラ通信シーケンス（デジタルモード）

```
PSX → Controller:  0x01  0x42  0x00  0x00  0x00
Controller → PSX:  0xFF  0x41  0x5A  btn1  btn2
                    ↑     ↑     ↑     ↑     ↑
                   HiZ   ID-Lo ID-Hi Data1 Data2
ACK pulses:              ___/‾\_/‾\_/‾\_/‾\___
```

**ID Bytes**:
- `0x41` = Digital Controller (5 bytes total)
- `0x73` = Analog Controller (9 bytes total)

**Button Data** (Active LOW = 押下時に0):
```c
buttons1 (byte 3):
  bit 0 = SELECT
  bit 1 = L3 (analog mode only)
  bit 2 = R3 (analog mode only)
  bit 3 = START
  bit 4 = UP
  bit 5 = RIGHT
  bit 6 = DOWN
  bit 7 = LEFT

buttons2 (byte 4):
  bit 0 = L2
  bit 1 = R2
  bit 2 = L1
  bit 3 = R1
  bit 4 = Triangle
  bit 5 = Circle
  bit 6 = Cross
  bit 7 = Square
```

---

## 実装要件

### 必須機能

1. **デジタルコントローラのエミュレーション**
   - 14個のボタン入力を読み取り
   - PSXの通信プロトコルに従って応答
   - 正確なタイミングでACKパルスを送信

2. **デュアルコア構成**
   - **Core 0**: ボタン入力のポーリングとメイン処理
   - **Core 1**: PSX通信処理（SELECT信号の監視とバイト送受信）

3. **SELECT信号による割り込み駆動**
   - SELECT立ち上がりエッジで割り込み発生
   - 現在のトランザクションをリセット
   - DATとACKをHi-Z状態にして次のトランザクションに備える

4. **完全なビットバンギング実装**
   - CLK立ち上がりエッジでCMDラインからコマンドビットを読み取り
   - CLK立ち下がりエッジでDATラインにレスポンスビットを出力
   - 各バイト転送後にACKパルスを生成

5. **オープンドレイン制御**
   - DATとACKは常にgpio_put()で0を出力
   - Hi-Z: `gpio_set_dir(pin, GPIO_IN)`
   - LOW: `gpio_set_dir(pin, GPIO_OUT)`

6. **メモリーカード共存のための配慮**
   - SELECT非選択時（HIGH）は完全にHi-Z
   - 他デバイスへのトランザクション中は一切バスに干渉しない
   - SELECT確認後の最小遅延でトランザクション開始

### パフォーマンス要件

- CLKエッジ検出の遅延: 最大500ns
- ACK応答時間: CMDバイト受信後2-3μs以内
- ボタンポーリング頻度: 最低1kHz（1ms間隔）

### エラーハンドリング

- 不正なコマンドには応答しない
- タイムアウト処理（通信中断時の復帰）
- SELECTのチャタリング対策

---

## ファイル構成

新規プロジェクトとして以下の構造で作成してください：

```
psx-bitbang-controller/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── README.md
└── src/
    ├── main.c                    // Core0: メインループとボタンポーリング
    ├── psx_protocol.c/h          // Core1: PSX通信プロトコル実装
    ├── psx_bitbang.c/h           // ビットバンギング低レベル関数
    ├── button_input.c/h          // ボタン入力処理
    ├── shared_state.c/h          // コア間共有データ構造
    └── config.h                  // ピン定義と設定
```

### 各ファイルの役割

#### `config.h`
- 全GPIOピン定義
- タイミング定数
- コントローラモード設定

#### `psx_bitbang.c/h`
- CLKエッジ検出
- CMDビット読み取り
- DATビット送信
- ACKパルス生成
- オープンドレイン制御関数

#### `psx_protocol.c/h`
- コマンド解析
- レスポンス生成
- バイト送受信ループ
- SELECT割り込みハンドラ

#### `button_input.c/h`
- GPIOからボタン状態読み取り
- ボタンデータのPSXフォーマット変換

#### `shared_state.c/h`
- ロックフリーなデータ共有
- ダブルバッファリング

---

## 実装ガイドライン

### 1. タイミングクリティカルな関数

以下の関数には `__time_critical_func()` マクロを付けてRAM実行させる：

```c
__time_critical_func(uint8_t psx_receive_byte(void))
__time_critical_func(void psx_send_byte(uint8_t data))
__time_critical_func(void psx_send_ack(void))
__time_critical_func(void sel_interrupt_handler(void))
```

### 2. CLKエッジ検出パターン

```c
// CLK立ち上がりエッジを待つ（タイムアウト付き）
static inline bool wait_clk_rising(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    while (gpio_get(PIN_CLK) == 0) {
        if ((time_us_32() - start) > timeout_us) return false;
    }
    return true;
}

// CLK立ち下がりエッジを待つ
static inline bool wait_clk_falling(uint32_t timeout_us) {
    uint32_t start = time_us_32();
    while (gpio_get(PIN_CLK) == 1) {
        if ((time_us_32() - start) > timeout_us) return false;
    }
    return true;
}
```

### 3. バイト送受信のサンプル構造

```c
uint8_t psx_receive_byte(void) {
    uint8_t data = 0;
    for (int bit = 0; bit < 8; bit++) {
        if (!wait_clk_rising(100)) return 0xFF; // timeout
        // Read CMD bit
        if (gpio_get(PIN_CMD)) {
            data |= (1 << bit);
        }
    }
    return data;
}

void psx_send_byte(uint8_t data) {
    for (int bit = 0; bit < 8; bit++) {
        if (!wait_clk_falling(100)) return; // timeout
        // Set DAT line
        if (data & (1 << bit)) {
            gpio_set_dir(PIN_DAT, GPIO_IN);  // Hi-Z = 1
        } else {
            gpio_set_dir(PIN_DAT, GPIO_OUT); // LOW = 0
        }
    }
}
```

### 4. ACKパルス生成

```c
void psx_send_ack(void) {
    busy_wait_us_32(2);  // Wait 2μs after byte
    gpio_set_dir(PIN_ACK, GPIO_OUT); // Assert ACK (LOW)
    busy_wait_us_32(2);              // Hold for 2μs
    gpio_set_dir(PIN_ACK, GPIO_IN);  // Release (Hi-Z)
}
```

### 5. SELECT割り込みハンドラ

```c
void sel_interrupt_handler(void) {
    gpio_acknowledge_irq(PIN_SEL, GPIO_IRQ_EDGE_RISE);
    
    // Immediately release bus
    gpio_set_dir(PIN_DAT, GPIO_IN);
    gpio_set_dir(PIN_ACK, GPIO_IN);
    
    // Reset transaction state
    transaction_active = false;
}
```

---

## コア間通信

### 共有データ構造

```c
typedef struct {
    uint8_t buttons1;  // PSX format
    uint8_t buttons2;  // PSX format
    volatile uint32_t write_index;
    volatile uint32_t read_index;
} shared_controller_state_t;

// Double buffering for lock-free access
shared_controller_state_t state_buffer[2];
```

### Core 0 (ボタンポーリング)

```c
void core0_main(void) {
    while (1) {
        // Read all buttons
        uint8_t btn1 = read_buttons_byte1();
        uint8_t btn2 = read_buttons_byte2();
        
        // Write to inactive buffer
        uint32_t write_idx = 1 - state_buffer[0].read_index;
        state_buffer[write_idx].buttons1 = btn1;
        state_buffer[write_idx].buttons2 = btn2;
        state_buffer[0].write_index = write_idx;
        
        sleep_ms(1); // 1kHz polling
    }
}
```

### Core 1 (PSX通信)

```c
void core1_main(void) {
    while (1) {
        // Wait for SELECT LOW
        while (gpio_get(PIN_SEL) == 1) tight_loop_contents();
        
        // Read stable state
        uint32_t read_idx = state_buffer[0].write_index;
        state_buffer[0].read_index = read_idx;
        
        // Process PSX transaction
        psx_process_transaction(&state_buffer[read_idx]);
    }
}
```

---

## デバッグとテスト

### LEDステータス表示

```c
typedef enum {
    LED_IDLE,           // OFF
    LED_ACTIVE,         // Solid ON - transaction in progress
    LED_ERROR,          // Fast blink - error condition
    LED_MEMCARD_DETECT  // Slow blink - memory card access detected
} led_status_t;
```

### シリアルデバッグ出力（オプション）

```c
// Debug output via USB serial (stdio)
#define DEBUG_ENABLED 0  // Set to 1 for debugging

#if DEBUG_ENABLED
    printf("CMD: 0x%02X, Response: 0x%02X\n", cmd, response);
#endif
```

---

## ビルド設定

### CMakeLists.txt の要件

```cmake
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(psx_bitbang_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(psx_controller
    src/main.c
    src/psx_protocol.c
    src/psx_bitbang.c
    src/button_input.c
    src/shared_state.c
)

target_link_libraries(psx_controller
    pico_stdlib
    pico_multicore
    hardware_gpio
    hardware_timer
)

pico_add_extra_outputs(psx_controller)
pico_enable_stdio_usb(psx_controller 1)
pico_enable_stdio_uart(psx_controller 0)
```

---

## 期待される動作

1. **電源投入時**
   - 全GPIOピンを初期化
   - DATとACKをHi-Z状態に
   - Core1を起動してPSX通信待機

2. **コントローラポーリング時**
   - PSXからSELECT LOW、コマンド `0x01 0x42` を受信
   - ID `0x41 0x5A` とボタンデータ2バイトを返送
   - 各バイト後にACKパルスを送信

3. **メモリーカードアクセス時**
   - SELECT LOWだが最初のバイトが `0x81`
   - コントローラは応答せず、DATとACKをHi-Zに維持

4. **SELECT解除時**
   - SELECT立ち上がりエッジで割り込み
   - 即座にDATとACKをHi-Z化
   - 次のトランザクションに備える

---

## 成功基準

- ✅ PS1/PS2実機でコントローラとして認識される
- ✅ 全14ボタンの入力が正しく反映される
- ✅ メモリーカードと同時接続しても正常動作
- ✅ メモリーカードの読み書きに干渉しない
- ✅ 長時間動作しても安定している

---

## 参考資料

- [PSX-SPX Documentation](https://psx-spx.consoledev.net/controllersandmemorycards/)
- [Raspberry Pi Pico C/C++ SDK](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf)
- [GameSX PSX Controller Data](https://gamesx.com/controldata/psxcont/psxcont.htm)

---

## 注意事項

- **PIOは一切使用しない** - 全てGPIO直接制御で実装
- **ピン番号は上記仕様通り** - 変更不可
- **オープンドレイン動作を正確に実装** - メモリーカード共存の鍵
- **タイミングは厳密に** - CLKエッジとの同期が重要
- **Core1は通信専用** - できるだけ他の処理を入れない

以上の仕様に基づいて、完全に動作するビットバンギング実装のPSXコントローラシミュレータプロジェクトを作成してください。
