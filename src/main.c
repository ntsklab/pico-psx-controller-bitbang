#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "config.h"
#include "shared_state.h"
#include "button_input.h"
#include "psx_protocol.h"

// ============================================================================
// LED Status Management
// ============================================================================

static led_status_t current_led_status = LED_IDLE;

void led_init(void) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // Start OFF
}

void led_set_status(led_status_t status) {
    current_led_status = status;
}

void led_update(void) {
    static uint32_t pattern_start = 0;
    static uint32_t last_blink = 0;
    static uint8_t blink_count = 0;
    static bool blink_state = false;
    uint32_t now = time_us_32();
    
#if DEBUG_ENABLED
    // Debug mode: LED ON only during POLLING, OFF otherwise
    switch (current_led_status) {
        case LED_POLLING:
        case LED_ACTIVE:
            gpio_put(LED_PIN, 1);  // ON during polling
            break;
            
        default:
            gpio_put(LED_PIN, 0);  // OFF for all other states
            break;
    }
#else
    // Non-debug mode: Blink pattern (100ms ON, 300ms cycle)
    // LED_READY = 1 blink, LED_POLLING = 2 blinks, LED_ERROR = 3 blinks
    
    uint8_t target_blinks = 0;
    switch (current_led_status) {
        case LED_READY:
        case LED_IDLE:
            target_blinks = 1;
            break;
        case LED_POLLING:
        case LED_ACTIVE:
            target_blinks = 2;
            break;
        case LED_ERROR:
            target_blinks = 3;
            break;
        case LED_MEMCARD_DETECT:
            target_blinks = 1;
            break;
    }
    
    uint32_t time_in_pattern = now - pattern_start;
    
    // Pattern duration: target_blinks * 300ms + 700ms pause = total cycle
    uint32_t pattern_duration = (target_blinks * 300000) + 700000;
    
    if (time_in_pattern >= pattern_duration) {
        // Restart pattern
        pattern_start = now;
        time_in_pattern = 0;
        blink_count = 0;
    }
    
    // Calculate current blink phase
    uint32_t blink_phase = time_in_pattern % 300000;  // 300ms per blink
    uint32_t current_blink = time_in_pattern / 300000;
    
    if (current_blink < target_blinks) {
        // Active blink period
        if (blink_phase < 100000) {
            gpio_put(LED_PIN, 1);  // ON for 100ms
        } else {
            gpio_put(LED_PIN, 0);  // OFF for 200ms
        }
    } else {
        // Pause period
        gpio_put(LED_PIN, 0);
    }
#endif
}

// ============================================================================
// Core 1 Entry Point - PSX Communication Handler
// ============================================================================

void core1_entry(void) {
    // Initialize PSX protocol
    psx_protocol_init();
    
#if DEBUG_ENABLED
    printf("Core 1: PSX protocol initialized\n");
#endif
    
    // Run protocol task (never returns)
    psx_protocol_task();
}

// ============================================================================
// Core 0 Main - Button Polling and System Management
// ============================================================================

int main(void) {
    // Initialize standard I/O (USB serial for debugging)
    stdio_init_all();
    
    // Small delay to allow USB to initialize
    sleep_ms(100);
    
#if DEBUG_ENABLED
    printf("\n\n");
    printf("==========================================\n");
    printf("  PSX Controller Bit-Banging Simulator\n");
    printf("==========================================\n");
    printf("System Clock: %d MHz\n", clock_get_hz(clk_sys) / 1000000);
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("\nGPIO Pin Assignment:\n");
    printf("  DAT: GPIO %d (Open-drain)\n", PIN_DAT);
    printf("  CMD: GPIO %d (Input)\n", PIN_CMD);
    printf("  SEL: GPIO %d (Input)\n", PIN_SEL);
    printf("  CLK: GPIO %d (Input)\n", PIN_CLK);
    printf("  ACK: GPIO %d (Open-drain)\n", PIN_ACK);
    printf("  LED: GPIO %d\n", LED_PIN);
    printf("\n");
#endif
    
    // Initialize LED
    led_init();
    led_set_status(LED_READY);
    
    // Initialize button inputs
    button_input_init();
    
#if DEBUG_ENABLED
    printf("Core 0: Button inputs initialized\n");
#endif
    
    // Initialize shared state
    shared_state_init();
    
#if DEBUG_ENABLED
    printf("Core 0: Shared state initialized\n");
#endif
    
    // Launch Core 1 for PSX communication
    multicore_launch_core1(core1_entry);
    
#if DEBUG_ENABLED
    printf("Core 0: Core 1 launched\n");
    printf("\n");
    printf("System ready. Waiting for PSX...\n");
    printf("\n");
#endif
    
    // Core 0 main loop - button polling
    uint32_t loop_count = 0;
    uint32_t last_stats_print = 0;
    
    // Button sampling statistics
    uint32_t sample_count = 0;
    uint32_t last_sample_time = 0;
    uint32_t min_sample_interval = 0;
    uint32_t max_sample_interval = 0;
    uint64_t total_sample_interval = 0;
    
    // Time-based sampling control
    uint32_t next_sample_time = time_us_32();
    
    // Button state variables
    uint8_t btn1 = 0xFF;
    uint8_t btn2 = 0xFF;
    
    while (1) {
        // Check if it's time to sample buttons
        uint32_t current_time = time_us_32();
        if ((int32_t)(next_sample_time - current_time) <= 0) {
            // Time to sample - read button states
            btn1 = button_read_byte1();
            btn2 = button_read_byte2();
            
            // Calculate actual sampling interval
            if (last_sample_time != 0) {
                uint32_t interval = current_time - last_sample_time;
                if (min_sample_interval == 0 || interval < min_sample_interval) {
                    min_sample_interval = interval;
                }
                if (interval > max_sample_interval) {
                    max_sample_interval = interval;
                }
                total_sample_interval += interval;
                sample_count++;
            }
            last_sample_time = current_time;
            
            // Schedule next sample
            next_sample_time += BUTTON_POLL_INTERVAL_US;
            
            // Write to shared state for Core 1
            shared_state_write(btn1, btn2);
        }
        
        // Update LED and statistics
        static uint64_t last_trans_count = 0;
        static uint32_t last_activity_time = 0;
        uint32_t now = time_us_32();
        
        psx_stats_t stats;
        psx_get_stats(&stats);
        
        if (stats.total_transactions > last_trans_count) {
            // Activity detected - briefly turn on LED
            led_set_status(LED_POLLING);
            last_trans_count = stats.total_transactions;
            last_activity_time = now;
        } else if ((now - last_activity_time) > 1000) {
            // No activity for 1ms - turn off LED (show ready state)
            if (stats.invalid_transactions > 0 || stats.timeout_errors > 0) {
                led_set_status(LED_ERROR);
            } else {
                led_set_status(LED_READY);
            }
        }
        
        led_update();
        
        // Debug output every 2 seconds
#if DEBUG_ENABLED
        loop_count++;
        if ((now - last_stats_print) > 2000000) {
            
            printf("\n=== Stats (loop=%lu) ===\n", loop_count);
            printf("Total Trans:  %llu\n", stats.total_transactions);
            printf("Controller:   %llu\n", stats.controller_transactions);
            printf("MemCard:      %llu\n", stats.memcard_transactions);
            printf("Invalid:      %llu\n", stats.invalid_transactions);
            printf("Timeout:      %llu\n", stats.timeout_errors);
            if (stats.invalid_transactions > 0) {
                printf("Last Invalid Addr: 0x%02X, Cmd: 0x%02X\n", stats.last_invalid_addr, stats.last_invalid_cmd);
            }
            
            // Transaction interval statistics
            if (stats.controller_transactions > 0) {
                printf("PSX Interval (us): Min=%lu, Max=%lu, Avg=%lu\n", 
                       stats.min_interval_us, stats.max_interval_us, stats.avg_interval_us);
                printf("PSX Polling Rate:  %.2f Hz\n", 1000000.0f / stats.avg_interval_us);
            }
            
            // Button sampling statistics
            printf("BTN Target Rate:   %.2f Hz (%lu us)\n", 
                   1000000.0f / BUTTON_POLL_INTERVAL_US, (uint32_t)BUTTON_POLL_INTERVAL_US);
            if (sample_count > 0) {
                uint32_t avg_sample_interval = (uint32_t)(total_sample_interval / sample_count);
                printf("BTN Interval (us): Min=%lu, Max=%lu, Avg=%lu\n", 
                       min_sample_interval, max_sample_interval, avg_sample_interval);
                printf("BTN Sample Rate:   %.2f Hz (actual)\n", 1000000.0f / avg_sample_interval);
            }
            
            printf("Buttons:      0x%02X 0x%02X\n", btn1, btn2);
            
            // Show individual button states
            printf("Pressed: ");
            if (!(btn1 & 0x01)) printf("SELECT ");
            if (!(btn1 & 0x08)) printf("START ");
            if (!(btn1 & 0x10)) printf("UP ");
            if (!(btn1 & 0x20)) printf("RIGHT ");
            if (!(btn1 & 0x40)) printf("DOWN ");
            if (!(btn1 & 0x80)) printf("LEFT ");
            if (!(btn2 & 0x01)) printf("L2 ");
            if (!(btn2 & 0x02)) printf("R2 ");
            if (!(btn2 & 0x04)) printf("L1 ");
            if (!(btn2 & 0x08)) printf("R1 ");
            if (!(btn2 & 0x10)) printf("△ ");
            if (!(btn2 & 0x20)) printf("○ ");
            if (!(btn2 & 0x40)) printf("☓ ");
            if (!(btn2 & 0x80)) printf("□ ");
            printf("\n");
            
            // Reset interval statistics for next period
            psx_reset_interval_stats();
            sample_count = 0;
            min_sample_interval = 0;
            max_sample_interval = 0;
            total_sample_interval = 0;
            
            last_stats_print = now;
        }
#endif
    }
    
    return 0;
}
