# PSX Controller Bit-Banging Simulator - AI Implementation Guide

## このドキュメントの目的

このドキュメントは、**AIアシスタントに同等のプロジェクトを再実装させる**ための詳細な指示書です。

### なぜこのドキュメントが必要か

PSXコントローラーのビットバンギング実装は、以下の理由で一般的な知識だけでは実装が困難です：
1. 仕様書通りの実装では動作しない（ACKタイミング、GPIO初期化など）
2. デバッグ出力がタイミング違反を引き起こし、原因特定が困難
3. メモリーカード共存の実装方法が仕様書に記載されていない

このドキュメントでは、実際に発生した**8つの主要問題とその解決策**を明記することで、AIが試行錯誤なしに動作する実装を生成できるようにしています。

### AIへの指示方法

```
あなたはRaspberry Pi Picoのエキスパートです。
以下のドキュメントに従って、PSX Controller Bit-Banging Simulatorを実装してください。

[このドキュメント全体を貼り付け]

特に「問題X」として記載されている8つの問題と解決策を必ず実装に反映してください。
```

---

## プロジェクト目標

Raspberry Pi Pico (RP2040) で、PIOを使わない**純粋なビットバンギング（GPIO直接制御）**によるPS1/PS2デジタルコントローラシミュレータを作成する。

### 達成すべき機能要件
1. PS1/PS2実機でコントローラーとして認識される
2. ACK Auto-TuningによりPS1/PS2両対応（自動タイミング調整）
3. メモリーカードと同一ポートで共存できる
4. 新旧ゲームの両方で動作する
5. 14ボタンの入力を1kHzで高精度サンプリング
6. 1フレーム未満の短い入力も検出できる（オプション）
7. HitBox型格闘スティックのSOCD処理に対応
8. ポーリングレート等の統計情報を取得できる
9. シリアルコマンドでデバッグモード動的切り替え

### 成功基準（統計値で確認）
```
Invalid transactions: 0
Timeout errors: 0
PSX Polling Rate: 59-60 Hz
PSX Interval: Min/Max差が小さい (例: 16710-16720µs)
BTN Sample Rate: 1000 Hz ± 1%
```

---

## ハードウェア仕様

### 使用マイコン
- **Raspberry Pi Pico (RP2040)**
- システムクロック: 125MHz
- デュアルコア Cortex-M0+
- Pico C/C++ SDK 2.2.0以上

### PSX/PS2 バス信号ピン配置（固定）

```c
PIN_DAT = GPIO 3   // Data line (Open-drain, bidirectional)
PIN_CMD = GPIO 4   // Command line (Input from PSX)
PIN_SEL = GPIO 10  // Select/Chip Select (Input from PSX, Active LOW)
PIN_CLK = GPIO 6   // Clock (Input from PSX, ~250kHz)
PIN_ACK = GPIO 7   // Acknowledge (Open-drain output to PSX)
```

**重要**: PSX/PS2本体側に全信号線のプルアップ抵抗あり。Pico側の内部プルアップは使用しない（オープンドレイン動作のため）。

### ボタン入力GPIO配置（固定）

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

**全ボタンはアクティブLOW（押下時=LOW）、内部プルアップ有効**

### LED表示
- **GPIO 25** (Pico内蔵LED) - トランザクション状態表示

---

## PSX通信プロトコル仕様

### 基本タイミング

| パラメータ | 値 | 備考 |
|-----------|-----|------|
| クロック周波数 | PS1: ~250kHz, PS2: 250kHz-5MHz？？ | 可変 |
| クロック極性 | CPOL=1 | アイドル時HIGH |
| クロックフェーズ | CPHA=1 | 立ち下がりエッジで出力、立ち上がりで読み取り |
| ビット順序 | LSB first | |
| ACK固定遅延 | 5µs | 最終CLK後から（psx_send_ack内の固定値） |
| ACKパルス幅 | 1-6µs | **Auto-Tuningで自動調整** |
| ACKポストウェイト | 0-6µs | **Auto-Tuningで自動調整** |
| CLKタイムアウト | 100µs | |

### デバイスアドレッシング

PSXは最初のバイトでデバイスを選択：
- `0x01` = 標準コントローラ ← **このプロジェクトで実装**
- `0x81` = メモリーカード

**重要**: メモリーカードアドレス(0x81)を最優先でチェックし、該当する場合は即座にSEL HIGH待機に移行。

### コントローラ通信シーケンス（デジタルモード）

```
PSX → Controller:  0x01  0x42  0x00  0x00  0x00
Controller → PSX:  0xFF  0x41  0x5A  btn1  btn2
                    ↑     ↑     ↑     ↑     ↑
                   HiZ   ID-Lo ID-Hi Data1 Data2
ACK pulses:              ___/‾\_/‾\_/‾\_/‾\___
                             ↑    ↑    ↑    ↑
                            5µs  5µs  5µs  5µs delay
```

**ID Bytes**:
- `0x41` = Digital Controller (5 bytes)
- `0x5A` = ID High byte

**Button Data Format** (Active LOW = 0):
```c
buttons1 (byte 3):
  bit 0 = SELECT
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

### Config Mode (実装しない)

このプロジェクトはデジタルコントローラのみをエミュレート。Config mode コマンド (0x43-0x4F) は無視。

---

## アーキテクチャ設計

### デュアルコア構成

#### Core 0 - ボタンポーリングと統計管理
- 時刻ベース1kHzボタンサンプリング
- 共有メモリへのボタンデータ書き込み
- LED状態管理
- 統計情報収集・表示
- デバッグ出力 (オプション)

#### Core 1 - PSX通信専用
- SELECT信号監視
- デバイスアドレス判定
- ビットバンギングによるバイト送受信
- ACKパルス生成
- トランザクション間隔統計

### モジュール構成

```
src/
├── main.c              // Core0: ボタンポーリング、LED制御、統計表示
├── psx_protocol.c/h    // Core1: プロトコル層、トランザクション管理
├── psx_bitbang.c/h     // 低レベル: CLKエッジ検出、バイト送受信
├── button_input.c/h    // ボタン入力読み取り、PSXフォーマット変換
├── shared_state.c/h    // ロックフリー共有メモリ、SOCD Cleaner
└── config.h            // 全設定定数、ピン定義
```

---

## 重要な実装ポイント（AI実装時の必須指示）

### 問題1: GPIO初期化順序の不備 → Invalid/Timeout発生

**現象**: gpio_init()のみでは初期化が不完全で、通信が不安定になる。

**解決策**: 必ずこの順序で初期化する
```c
gpio_set_function(PIN_X, GPIO_FUNC_SIO);  // 1. SIO機能を明示的に設定
gpio_init(PIN_X);                          // 2. GPIO初期化
gpio_set_dir(PIN_X, GPIO_IN/OUT);         // 3. 方向設定
```

**なぜ重要か**: gpio_init()内で暗黙的にgpio_set_function()が呼ばれるが、事前に明示することで初期化の確実性が向上。特にDAT/ACKピンで重要。

### 問題2: オープンドレイン制御の誤実装 → バス競合

**現象**: 複数デバイス（コントローラー+メモリーカード）でバス競合が発生。

**解決策**: gpio_put()とgpio_set_dir()を組み合わせる
```c
// 初期化時: 出力値を常に0に固定
gpio_put(PIN_DAT, 0);
gpio_put(PIN_ACK, 0);

// Hi-Z (HIGH) 状態 = 入力モード
gpio_set_dir(PIN_DAT, GPIO_IN);

// LOW出力 = 出力モード
gpio_set_dir(PIN_DAT, GPIO_OUT);
```

**禁止事項**:
- ❌ 内部プルアップを有効にしてはいけない（オープンドレイン動作を妨げる）
- ❌ gpio_put(pin, 1)でHIGHを出力してはいけない（バス競合の原因）
- ✅ PSX/PS2本体側にプルアップ回路あり（追加の外部抵抗は不要）

### 問題3: メモリーカードとの干渉 → メモリーカード読み書き失敗

**現象**: メモリーカード接続時、コントローラーがバスに干渉してデータ破損。

**根本原因**: PSXは同一バスでアドレス0x01(コントローラー)と0x81(メモリーカード)を切り替える。コントローラーがメモリーカードトランザクション中にバスを操作すると干渉する。

**解決策**: メモリーカードアドレスを最優先でチェックし、該当時は完全に待機
```c
uint8_t addr = psx_receive_byte();

// メモリーカードアドレスを最初にチェック（コントローラーより優先）
if (addr == 0x81) {
    stats.memcard_transactions++;
    
    // 即座にバス解放
    psx_release_bus();
    
    // SEL HIGH（トランザクション終了）まで待機
    // この間、メモリーカードが通信している
    while (!psx_read_sel() && transaction_active) {
        tight_loop_contents();
    }
    
    transaction_active = false;
    continue;  // 次のSEL LOWトランザクションへ
}

// コントローラーアドレス(0x01)の処理はその後
if (addr == 0x01) {
    // コントローラートランザクション処理
}
```

**重要**: メモリーカードのトランザクション全体が終わるまで、コントローラーは一切バスに触れない。

### 問題4: 古いゲームでコントローラー未認識 → ACKタイミング不適合

**現象**: 新しいゲームでは動作するが、古いPS1ゲームで認識されない。

**原因**: 仕様書では「ACK遅延2µs、パルス幅2µs」だが、古いゲームはより長いACKを期待。

**解決策**: ACKタイミングを延長
```c
// config.h
#define ACK_DELAY_US        5    // 最終CLKパルス後の遅延（2µs→5µsに増加）
#define ACK_PULSE_WIDTH_US  5    // ACKパルス幅（2µs→5µsに増加）

// psx_bitbang.c
void psx_send_ack(void) {
    busy_wait_us_32(ACK_DELAY_US);        // バイト受信後、待機
    gpio_set_dir(PIN_ACK, GPIO_OUT);      // ACK=LOW
    busy_wait_us_32(ACK_PULSE_WIDTH_US);  // パルス幅
    gpio_set_dir(PIN_ACK, GPIO_IN);       // ACK=Hi-Z
}
```

**調整結果**:
- 2µs/3µs: 新しいゲームのみ動作
- 5µs/5µs: 新旧ゲーム両対応 ← **この値を使用**

**注意**: PSXはACK受信後、次のバイト送信まで約10-15µsかかる場合がある。ACK後に20µs待機を追加すると安定性が向上。

### 問題5: Invalid/Timeout大量発生 → printf()によるタイミング違反

**現象**: デバッグ出力を追加すると、Invalid transactions や Timeout errors が大量に発生。

**根本原因**: printf()はUSB経由で数百µs～数msかかる。PSXクロックは~250kHz（4µs/bit）なので、printf()実行中にCLKエッジを見逃す。

**解決策**: タイミングクリティカルな関数から全てのprintf()を削除
```c
// psx_bitbang.c
__time_critical_func(uint8_t psx_receive_byte(void)) {
    uint8_t data = 0;
    for (int bit = 0; bit < 8; bit++) {
        // ❌ printf("bit=%d\n", bit); は絶対に入れない
        
        // CLKエッジ待機
        if (!wait_clk_rising(PSX_CLK_TIMEOUT_US)) {
            return 0xFF;  // タイムアウト
        }
        
        // データ読み取り
        if (gpio_get(PIN_CMD)) {
            data |= (1 << bit);
        }
    }
    return data;
}

// psx_transfer_byte(), psx_send_ack()も同様
```

**許可される場所**: トランザクション完全終了後（SEL HIGH後）のみprintf()可能。

**統計での確認**: Invalid: 0, Timeout: 0 になるまでprintf()を削除する。

### 問題6: トランザクション中断時の応答遅延 → 次のトランザクションに影響

**現象**: PSXがトランザクションを中断（SEL HIGH）した後、次のトランザクションで遅延。

**解決策**: SEL立ち上がりエッジ割り込みで即座にバス解放
```c
// 初期化時に割り込み設定
gpio_set_irq_enabled_with_callback(PIN_SEL, GPIO_IRQ_EDGE_RISE, true, 
                                   &psx_sel_interrupt_handler);

// 割り込みハンドラ
void psx_sel_interrupt_handler(unsigned int gpio_num, uint32_t events) {
    if (gpio_num == PIN_SEL && (events & GPIO_IRQ_EDGE_RISE)) {
        // 即座にバス解放（他の処理より優先）
        gpio_set_dir(PIN_DAT, GPIO_IN);
        gpio_set_dir(PIN_ACK, GPIO_IN);
        
        // トランザクション状態をリセット
        transaction_active = false;
    }
}
```

**重要性**: 
- SEL HIGHは「トランザクション終了」を意味し、即座にバスを解放する必要がある
- ポーリングでSEL状態を確認する方法では遅延が大きい
- 割り込みハンドラ内では最小限の処理のみ（バス解放とフラグ設定）

---

## ボタン入力処理の問題と解決

### 問題7: ボタンサンプリング間隔が不安定 → 最小475µs、最大1524µs

**現象**: sleep_ms(1)で1kHzサンプリングを実装したが、デバッグ出力で間隔が大きくばらつく。

**原因**: 
- sleep_ms()は「最低1ms待つ」だけで、その後の処理時間は含まれない
- printf()が数百µs消費すると、次のサンプリングが遅れる

**解決策**: 時刻ベースのスケジューリング
```c
// config.h
#define BUTTON_POLL_INTERVAL_US 1000  // 1kHz = 1000µs間隔

// main.c - Core0メインループ
uint32_t next_sample_time = time_us_32();  // 次のサンプリング時刻

while (1) {
    uint32_t current_time = time_us_32();
    
    // スケジュール時刻に到達したらサンプリング実行
    if ((int32_t)(next_sample_time - current_time) <= 0) {
        // ボタン読み取り
        uint8_t btn1 = button_read_byte1();
        uint8_t btn2 = button_read_byte2();
        
        // 共有メモリに書き込み
        shared_state_write(btn1, btn2);
        
        // 次のサンプリング時刻をスケジュール（累積誤差なし）
        next_sample_time += BUTTON_POLL_INTERVAL_US;
    }
    
    // サンプリング待機中もLED更新や統計処理を継続
    led_update();
    // 統計処理など
}
```

**効果**:
- デバッグ出力に関係なく、1000µs±1µsの精度でサンプリング
- 処理時間が長くても次のスケジュールは正確
- printf()がサンプリング精度に影響しない

### 問題8: 1フレーム未満の短い入力が取りこぼされる

**現象**: PSXは約60Hz(16.7ms間隔)でポーリング。ボタンは1kHzでサンプリング。しかし、PSXポーリング間に「押して離す」と検出されない。

**例**:

|時刻    | 0ms  | 5ms  | 10ms | 15ms | 16.7ms|
| :---: | :---: | :---: | :---: | :---: | :---: |
|ボタン  | OFF  | ON   | ON  | OFF  | OFF |
|サンプル | ↓  |  ↓  |  ↓  |  ↓  |  ↓ |
|メモリ内ボタン状況 |  OFF  | ON   | OFF  | OFF  | OFF |
|PSXポーリング |↓| |||↓|
|PSX認識ボタン | OFF |→|→|→|OFF|

結果: PSXポーリング時にOFFなので、入力は無視される


**解決策**: ボタンラッチングモード
```c
// config.h
#define BUTTON_LATCHING_MODE 0  // 0=Direct（通常）, 1=Latching（格闘ゲーム）

// shared_state.c
#if BUTTON_LATCHING_MODE
// ラッチ用変数（PSXが読むまで押下状態を保持）
static uint8_t latched_btn1 = 0xFF;
static uint8_t latched_btn2 = 0xFF;

void shared_state_write(uint8_t btn1, uint8_t btn2) {
    // ビットAND: 一度でも0(押下)になったら0を保持
    // 0xFF & 0xFE = 0xFE (bit0が0=押下を保持)
    latched_btn1 &= btn1;
    latched_btn2 &= btn2;
    
    // ラッチされた状態をバッファに書き込み
    buffer[write_idx].buttons1 = latched_btn1;
    buffer[write_idx].buttons2 = latched_btn2;
}

void shared_state_read(uint8_t *btn1, uint8_t *btn2) {
    // PSXが読み取る
    *btn1 = buffer[read_idx].buttons1;
    *btn2 = buffer[read_idx].buttons2;
    
    // 読み取り後、ラッチをクリア（次の押下に備える）
    latched_btn1 = 0xFF;
    latched_btn2 = 0xFF;
}
#else
// Direct mode: 現在の状態をそのまま書き込む
void shared_state_write(uint8_t btn1, uint8_t btn2) {
    buffer[write_idx].buttons1 = btn1;
    buffer[write_idx].buttons2 = btn2;
}
#endif
```

**効果**:
- Latching mode: 1フレーム(16.7ms)未満の短い入力も確実に検出
- 格闘ゲームの1フレーム技など、高精度入力が必要な用途で有効
- Direct mode: 通常のゲームプレイに適した自然な挙動

### SOCD Cleaner (HitBoxみたいなレバーレスコントローラ対応)

```c
// shared_state.c - shared_state_read()内
// Left + Right = 両方解放（ニュートラル）
if (left_pressed && right_pressed) {
    *btn1 |= 0x80;  // LEFT解放
    *btn1 |= 0x20;  // RIGHT解放
}

// Up + Down = 両方解放（ニュートラル）
if (up_pressed && down_pressed) {
    *btn1 |= 0x10;  // UP解放
    *btn1 |= 0x40;  // DOWN解放
}
```

### 問題9: PS1/PS2でACKタイミング要件が異なる → Auto-Tuning実装

**現象**: 
- PS2は高速クロック(250kHz-5MHz？)で動作し、短いACKパルス(1-2µs)が必要
- PS1は低速クロック(250kHz)で動作し、長めのACKパルス(3-6µs)が必要
- 固定タイミングではどちらかで動作しない・・・と思ったけど3usで両方動く可能性あり。（じゃあいらないじゃん、自動調整・・・）

**解決策**: 起動時にACKタイミングを自動調整するAuto-Tuning機能

```c
// config.h - Auto-Tuning設定
#define ACK_AUTO_TUNE_ENABLED   1

#if ACK_AUTO_TUNE_ENABLED
// パラメータ探索範囲
#define ACK_PULSE_WIDTH_MIN     1           // 最小パルス幅 (1µs for PS2)
#define ACK_PULSE_WIDTH_MAX     6           // 最大パルス幅 (6µs for PS1)
#define ACK_POST_WAIT_MIN       0           // 最小ウェイト (0µs)
#define ACK_POST_WAIT_MAX       6           // 最大ウェイト (6µs)

// 評価基準
#define ACK_TUNE_TEST_TRANSACTIONS  8       // 各設定で8トランザクション試験
#define ACK_TUNE_CMD_SUCCESS_THRESHOLD 0.5  // 成功率50%以上
#define ACK_TUNE_IDLE_TIMEOUT_US    5000000 // 5秒間トランザクションなしでリセット
#endif

// psx_bitbang.c - Auto-Tuning状態管理
static volatile uint32_t current_ack_pulse_width = ACK_PULSE_WIDTH_MAX;
static volatile uint32_t current_ack_post_wait = ACK_POST_WAIT_MIN;
static volatile uint32_t test_cmd_success = 0;     // 成功カウント
static volatile uint32_t best_pulse_width = ACK_PULSE_WIDTH_MAX;
static volatile uint32_t best_post_wait = ACK_POST_WAIT_MIN;
static volatile float best_cmd_success_rate = -1.0f;
static volatile bool tuning_complete = false;

// Auto-Tuning評価関数
void psx_ack_tune_on_command(bool cmd_success) {
    if (tuning_complete) return;
    
    if (cmd_success) test_cmd_success++;
    
    // 8トランザクション経過したら評価
    if (test_addr_count >= ACK_TUNE_TEST_TRANSACTIONS) {
        float success_rate = (float)test_cmd_success / (float)test_addr_count;
        
        // 成功率が閾値以上なら候補として記録
        if (success_rate >= ACK_TUNE_CMD_SUCCESS_THRESHOLD) {
            bool is_better = false;
            
            // より高い成功率
            if (success_rate > best_cmd_success_rate) {
                is_better = true;
            }
            // 同じ成功率なら、短いWAIT優先、次に中間のPULSE優先
            else if (success_rate == best_cmd_success_rate) {
                if (current_ack_post_wait < best_post_wait) {
                    is_better = true;
                } else if (current_ack_post_wait == best_post_wait) {
                    uint32_t pulse_mid = (ACK_PULSE_WIDTH_MIN + ACK_PULSE_WIDTH_MAX) / 2;
                    int32_t current_dist = abs((int32_t)current_ack_pulse_width - (int32_t)pulse_mid);
                    int32_t best_dist = abs((int32_t)best_pulse_width - (int32_t)pulse_mid);
                    if (current_dist < best_dist) {
                        is_better = true;
                    }
                }
            }
            
            if (is_better) {
                best_pulse_width = current_ack_pulse_width;
                best_post_wait = current_ack_post_wait;
                best_cmd_success_rate = success_rate;
                printf("[ACK-TUNE] New best: PULSE=%lu, WAIT=%lu (%.1f%%)\n",
                       current_ack_pulse_width, current_ack_post_wait,
                       success_rate * 100.0f);
            }
        }
        
        // 次のパラメータ組み合わせに移行
        // 探索順序: PULSE=大→小、各PULSEでWAIT=小→大
        if (current_ack_post_wait < ACK_POST_WAIT_MAX) {
            current_ack_post_wait++;
        } else {
            current_ack_post_wait = ACK_POST_WAIT_MIN;
            if (current_ack_pulse_width > ACK_PULSE_WIDTH_MIN) {
                current_ack_pulse_width--;
            } else {
                // 全組み合わせ探索完了
                if (best_cmd_success_rate >= ACK_TUNE_CMD_SUCCESS_THRESHOLD) {
                    current_ack_pulse_width = best_pulse_width;
                    current_ack_post_wait = best_post_wait;
                    tuning_complete = true;
                    printf("[ACK-TUNE] LOCKED: PULSE=%lu us, WAIT=%lu us (%.0f%%)\n",
                           best_pulse_width, best_post_wait, best_cmd_success_rate * 100.0f);
                }
            }
        }
        
        // カウンタリセット
        test_addr_count = 0;
        test_cmd_success = 0;
    }
}

// ACK送信（現在のパラメータ使用）
void psx_send_ack(void) {
    busy_wait_us_32(5);  // 固定ウェイト（psx_send_ackの最初）
    busy_wait_us_32(current_ack_post_wait);  // 可変ウェイト
    gpio_out_low(PIN_ACK);
    busy_wait_us_32(current_ack_pulse_width);  // 可変パルス幅
    gpio_hi_z(PIN_ACK);
}
```

**効果**:
- PS1/PS2両対応、起動時に自動的に最適なタイミングを検出
- 成功率、ウェイト時間、パルス幅の優先順位で最適パラメータを選択
- LOCKED後はタイミング固定で安定動作

---

## デバッグモード動的切り替え

起動後にシリアルコマンドでデバッグモードをON/OFFできる機能。

```c
// main.c - グローバル変数
bool debug_mode = DEBUG_ENABLED;  // config.hのデフォルト値で初期化

// メインループでシリアル入力チェック
while (1) {
    int ch = getchar_timeout_us(0);  // ノンブロッキング読み取り
    if (ch != PICO_ERROR_TIMEOUT) {
        static char cmd_buffer[32];
        static uint8_t cmd_pos = 0;
        
        if (ch == '\r' || ch == '\n') {
            if (cmd_pos > 0) {
                cmd_buffer[cmd_pos] = '\0';
                
                if (strcmp(cmd_buffer, "debug") == 0) {
                    debug_mode = !debug_mode;
                    printf("\n>>> Debug mode: %s\n\n", debug_mode ? "ON" : "OFF");
                }
                
                cmd_pos = 0;
            }
        } else if (ch >= 32 && ch < 127 && cmd_pos < sizeof(cmd_buffer) - 1) {
            cmd_buffer[cmd_pos++] = ch;
        }
    }
    
    // ボタンサンプリング処理
    // ...
    
    // デバッグ出力（debug_modeで条件分岐）
    if (debug_mode) {
        // 統計情報表示
    }
}

// LED更新もdebug_modeとlatching_modeで動作変更
void led_update(void) {
    if (debug_mode) {
        // シンプル動作: ポーリング中のみ点灯
        gpio_put(LED_PIN, current_led_status == LED_POLLING ? 1 : 0);
    } else {
        // 点滅パターン動作（ラッチングモードで点滅回数変化）
        // ERROR時は高速点滅
    }
}
```

**利用可能なコマンド**:
```
debug [Enter]   # デバッグモード切り替え
latch [Enter]   # ラッチングモード切り替え
save [Enter]    # 設定をFlashに保存
help [Enter]    # ヘルプ表示
? [Enter]       # ヘルプ表示
```

**Flash保存機能**:
- `save`コマンドで現在の設定（debug_mode, latching_mode）をFlashに保存
- 次回起動時に自動的に読み込まれる
- Flashの最終セクター（4KB）を使用
- 保存時はCore1を一時停止し、完了後に自動再開

---

## LED制御

### デバッグモード有効時 (debug_mode = true)

```c
// main.c
void led_update(void) {
    if (debug_mode) {
        switch (current_led_status) {
            case LED_POLLING:
                gpio_put(LED_PIN, 1);  // ON during polling
                break;
            default:
                gpio_put(LED_PIN, 0);  // OFF otherwise
                break;
        }
    }
}

// トランザクション検出時にLED_POLLINGに設定、1ms後にLED_READYに戻す
```

### デバッグモード無効時 (debug_mode = false)

```c
// ブリンクパターン（ラッチングモードで変化）
// 1回点滅 = READY (待機中)
// 2回点滅 = POLLING (ラッチングOFF時)
// 3回点滅 = POLLING (ラッチングON時)
// 高速点滅 = ERROR (100ms ON / 100ms OFF)

void led_update(void) {
    if (current_led_status == LED_ERROR) {
        // Fast blink: 100ms ON, 100ms OFF (200ms cycle)
        uint32_t fast_phase = now % 200000;
        gpio_put(LED_PIN, fast_phase < 100000 ? 1 : 0);
    } else {
        // Pattern blinks
        uint8_t target_blinks = (current_led_status == LED_POLLING) 
                               ? (latching_mode ? 3 : 2)  // 3 or 2 blinks
                               : 1;                       // 1 blink for READY
        
        // 100ms ON + 200ms OFF per blink, then 700ms pause
        // Total cycle: target_blinks * 300ms + 700ms
    }
}
```

### LED表示まとめ

| デバッグモード | 状態 | LED動作 | 意味 |
|--------------|------|---------|------|
| ON | POLLING | 点灯 | トランザクション処理中 |
| ON | その他 | 消灯 | アイドル |
| OFF | READY | 1回点滅 | 準備完了 |
| OFF | POLLING (ラッチOFF) | 2回点滅 | ポーリング中（通常） |
| OFF | POLLING (ラッチON) | 3回点滅 | ポーリング中（格闘ゲーム） |
| OFF | ERROR | 高速点滅 | エラー発生 |

---

## 統計機能

### トランザクション統計

```c
typedef struct {
    uint64_t total_transactions;      // 累積トランザクション数
    uint64_t controller_transactions; // コントローラー向け
    uint64_t memcard_transactions;    // メモリーカード向け
    uint64_t invalid_transactions;    // 無効アドレス
    uint64_t timeout_errors;          // タイムアウト
    uint32_t min_interval_us;         // 最小ポーリング間隔
    uint32_t max_interval_us;         // 最大ポーリング間隔
    uint32_t avg_interval_us;         // 平均ポーリング間隔
} psx_stats_t;
```

### 間隔計測

```c
// psx_protocol.c - Pollコマンド(0x42)実行時のみ計測
if (cmd == PSX_CMD_POLL) {
    uint32_t current_time = time_us_32();
    if (last_transaction_time != 0) {
        uint32_t interval = current_time - last_transaction_time;
        
        // 最小/最大更新
        if (stats.min_interval_us == 0 || interval < stats.min_interval_us) {
            stats.min_interval_us = interval;
        }
        if (interval > stats.max_interval_us) {
            stats.max_interval_us = interval;
        }
        
        // 平均計算
        total_interval_sum += interval;
        interval_count++;
        stats.avg_interval_us = (uint32_t)(total_interval_sum / interval_count);
    }
    last_transaction_time = current_time;
}
```

### デバッグ出力例

```
=== Stats (loop=388223) ===
Total Trans:  23345
Controller:   23308
MemCard:      37
Invalid:      0
Timeout:      0
PSX Interval (us): Min=16710, Max=16718, Avg=16715
PSX Polling Rate:  59.83 Hz
BTN Target Rate:   1000.00 Hz (1000 us)
BTN Interval (us): Min=999, Max=1001, Avg=1000
BTN Sample Rate:   1000.00 Hz (actual)
Buttons:      0xFF 0xFF
Pressed: 
```

---

## ビルド設定

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_psx_controller_bitbang C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico-psx-controller-bitbang
    src/main.c
    src/psx_protocol.c
    src/psx_bitbang.c
    src/button_input.c
    src/shared_state.c
)

target_link_libraries(pico-psx-controller-bitbang
    pico_stdlib
    pico_multicore
    hardware_gpio
    hardware_timer
)

pico_add_extra_outputs(pico-psx-controller-bitbang)
pico_enable_stdio_usb(pico-psx-controller-bitbang 1)
pico_enable_stdio_uart(pico-psx-controller-bitbang 0)
```

### ビルドコマンド

```bash
mkdir build && cd build
cmake ..
ninja  # または make -j4
```

---

## トラブルシューティング

### 問題: Invalid/Timeout エラーが多発

**原因と対処**:
1. ✅ **printf()除去**: タイミング重要関数から全てのprintf()を削除済み
2. ✅ **ACKタイミング**: 5µs/5µsに調整済み（旧ゲーム対応）
3. ✅ **GPIO初期化順序**: gpio_set_function()を先に実行

### 問題: メモリーカードと干渉

**原因と対処**:
1. ✅ **メモリーカード優先**: 0x81アドレスを最初にチェック
2. ✅ **SEL HIGH待機**: メモリーカードトランザクション中は完全待機
3. ✅ **バス解放**: SEL HIGH時は必ずHi-Z状態

### 問題: ボタンサンプリングがばらつく

**原因と対処**:
1. ✅ **時刻ベースサンプリング**: sleep()ではなくtime_us_32()で制御
2. ✅ **DEBUG無効化**: 本番使用時はDEBUG_ENABLED=0を推奨

---

## 設定ガイド

### config.h 主要設定

```c
// デバッグ出力 (0=無効推奨, 1=有効)
#define DEBUG_ENABLED 0

// ボタン入力モード
// 0 = Direct (通常プレイ)
// 1 = Latching (格闘ゲーム推奨)
#define BUTTON_LATCHING_MODE 0

// ボタンサンプリング間隔 (µs)
#define BUTTON_POLL_INTERVAL_US 1000  // 1kHz

// ACKタイミング (µs) - 変更非推奨
#define ACK_DELAY_US        5
#define ACK_PULSE_WIDTH_US  5
```

### 用途別推奨設定

| 用途 | DEBUG_ENABLED | BUTTON_LATCHING_MODE |
|------|---------------|----------------------|
| 通常プレイ | 0 | 0 |
| 格闘ゲーム | 0 | 1 |
| 開発/検証 | 1 | 0 or 1 |

---

## 成功基準

### 動作確認項目
- ✅ PS1/PS2 BIOSでコントローラー認識
- ✅ 全14ボタンの正常動作
- ✅ メモリーカード同時使用可能
- ✅ 新旧ゲーム互換性
- ✅ Invalid: 0, Timeout: 0 (統計確認)
- ✅ PSXポーリングレート ~60Hz (統計確認)
- ✅ ボタンサンプリングレート ~1000Hz (統計確認)

### 統計での確認ポイント

```
理想的な統計値:
- Invalid transactions: 0
- Timeout errors: 0
- PSX Polling Rate: 59-60 Hz
- PSX Interval: Min/Max差が小さい (16710-16720µs程度)
- BTN Sample Rate: 1000 Hz ± 1%
```

---

## AI実装時のチェックリスト

AIにこのプロジェクトを実装させる際、以下を必ず指示・確認してください：

### 初期実装フェーズ
- [ ] GPIO初期化で`gpio_set_function()`を`gpio_init()`の前に実行
- [ ] DAT/ACKピンをオープンドレイン制御（gpio_set_dir()で切り替え）
- [ ] PSX本体側にプルアップ回路あり、Pico側の内部プルアップは禁止
- [ ] SEL立ち上がりエッジ割り込みでバス解放

### プロトコル実装フェーズ
- [ ] メモリーカードアドレス(0x81)を最優先チェック
- [ ] メモリーカードトランザクション中はSEL HIGH待機
- [ ] ACK Auto-Tuning実装（PULSE: 1-6µs, WAIT: 0-6µs探索）
- [ ] タイミングクリティカル関数から全printf()削除

### ボタン処理フェーズ
- [ ] 時刻ベースサンプリング（sleep()ではなくtime_us_32()）
- [ ] ボタンラッチングモード実装（config.hで切替可能）
- [ ] SOCD Cleaner実装（反対方向同時押し=ニュートラル）

### デバッグ・統計フェーズ
- [ ] トランザクション統計（total/controller/memcard/invalid/timeout）
- [ ] ポーリング間隔統計（min/max/avg、0x42コマンド実行時のみ計測）
- [ ] ボタンサンプリング統計（実測値）
- [ ] ACK Auto-Tuning状態表示（waiting.../tuning.../LOCKED）
- [ ] シリアルコマンドでデバッグモード動的切り替え（"debug"入力）

### 動作確認
- [ ] Invalid: 0, Timeout: 0 を達成
- [ ] PSXポーリングレート 59-60Hz
- [ ] ボタンサンプリングレート 1000Hz ±1%
- [ ] メモリーカード同時接続で動作

---

## ライセンス

このプロジェクトはGPL-3.0ライセンスの下で公開されています。

```
Copyright (C) 2024-2025 ntsklab

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

---

## 参考資料

- [PSX-SPX Documentation](https://psx-spx.consoledev.net/controllersandmemorycards/)
- [Raspberry Pi Pico Datasheet](https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Pico C/C++ SDK](https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf)
- [GameSX PSX Controller Data](https://gamesx.com/controldata/psxcont/psxcont.htm)
- [PicoGamepadConverter](https://github.com/Loc15/PicoGamepadConverter)
- [PicoMemcard](https://github.com/dangiu/PicoMemcard/)

---

**このガイドは実際に動作確認済みの実装をベースに作成されています。**
