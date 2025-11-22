/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * Copyright (C) 2024-2025 ntsklab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
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

// ============================================================================
// Debug Mode Control
// ============================================================================

bool debug_mode = DEBUG_ENABLED; // Runtime debug mode flag

void led_init(void)
{
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0); // Start OFF
}

void led_set_status(led_status_t status)
{
    current_led_status = status;
}

void led_update(void)
{
    static uint32_t pattern_start = 0;
    static uint32_t last_blink = 0;
    static uint8_t blink_count = 0;
    static bool blink_state = false;
    uint32_t now = time_us_32();

    if (debug_mode)
    {
        // Debug mode: LED ON only during POLLING, OFF otherwise
        switch (current_led_status)
        {
        case LED_POLLING:
        case LED_ACTIVE:
            gpio_put(LED_PIN, 1); // ON during polling
            break;

        default:
            gpio_put(LED_PIN, 0); // OFF for all other states
            break;
        }
    }
    else
    {
        // Non-debug mode: Blink pattern (100ms ON, 300ms cycle)
        // LED_READY = 1 blink, LED_POLLING = 2 blinks, LED_ERROR = 3 blinks

        uint8_t target_blinks = 0;
        switch (current_led_status)
        {
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

        if (time_in_pattern >= pattern_duration)
        {
            // Restart pattern
            pattern_start = now;
            time_in_pattern = 0;
            blink_count = 0;
        }

        // Calculate current blink phase
        uint32_t blink_phase = time_in_pattern % 300000; // 300ms per blink
        uint32_t current_blink = time_in_pattern / 300000;

        if (current_blink < target_blinks)
        {
            // Active blink period
            if (blink_phase < 100000)
            {
                gpio_put(LED_PIN, 1); // ON for 100ms
            }
            else
            {
                gpio_put(LED_PIN, 0); // OFF for 200ms
            }
        }
        else
        {
            // Pause period
            gpio_put(LED_PIN, 0);
        }
    }
}

// ============================================================================
// Core 1 Entry Point - PSX Communication Handler
// ============================================================================

void core1_entry(void)
{
    // Initialize PSX protocol
    psx_protocol_init();

    // Run protocol task (never returns)
    psx_protocol_task();
}

// ============================================================================
// Core 0 Main - Button Polling and System Management
// ============================================================================

int main(void)
{
    // Initialize standard I/O (USB serial for debugging)
    stdio_init_all();

    // Small delay to allow USB to initialize
    sleep_ms(100);

    // Initialize LED
    led_init();
    led_set_status(LED_READY);

    // Initialize button inputs
    button_input_init();

    // Initialize shared state
    shared_state_init();


    // Launch Core 1 for PSX communication
    multicore_launch_core1(core1_entry);

    // Core 0 main loop - button polling
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

    printf("\n");
    printf("==========================================\n");
    printf("  PSX Controller Bit-Banging Simulator\n");
    printf("==========================================\n");
    printf("System ready.\n");
    printf("Type 'debug' to toggle debug mode\n");
    printf("Debug mode: %s\n", debug_mode ? "ON" : "OFF");
    printf("\n");

    while (1)
    {
        // Check for serial input (debug toggle command)
        int ch = getchar_timeout_us(0); // Non-blocking read
        if (ch != PICO_ERROR_TIMEOUT)
        {
            static char cmd_buffer[32];
            static uint8_t cmd_pos = 0;

            if (ch == '\r' || ch == '\n')
            {
                // Command complete
                if (cmd_pos > 0)
                {
                    cmd_buffer[cmd_pos] = '\0';

                    // Check for "debug" command
                    if (strcmp(cmd_buffer, "debug") == 0)
                    {
                        debug_mode = !debug_mode;
                        printf("\n>>> Debug mode: %s\n\n", debug_mode ? "ON" : "OFF");
                    }

                    cmd_pos = 0;
                }
            }
            else if (ch >= 32 && ch < 127 && cmd_pos < sizeof(cmd_buffer) - 1)
            {
                // Printable character
                cmd_buffer[cmd_pos++] = ch;
            }
        }
        // Check if it's time to sample buttons
        uint32_t current_time = time_us_32();
        if ((int32_t)(next_sample_time - current_time) <= 0)
        {
            // Time to sample - read button states
            btn1 = button_read_byte1();
            btn2 = button_read_byte2();

            // Calculate actual sampling interval
            if (last_sample_time != 0)
            {
                uint32_t interval = current_time - last_sample_time;
                if (min_sample_interval == 0 || interval < min_sample_interval)
                {
                    min_sample_interval = interval;
                }
                if (interval > max_sample_interval)
                {
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
        static bool activity_initialized = false;
        uint32_t now = time_us_32();

        psx_stats_t stats;
        psx_get_stats(&stats);

        // Initialize activity time on first run
        if (!activity_initialized)
        {
            last_activity_time = now;
            activity_initialized = true;
            // Set initial state to READY (no transactions yet)
            led_set_status(LED_READY);
        }

        // Check for controller transactions only (not memory card)
        if (stats.controller_transactions > last_trans_count)
        {
            // Controller activity detected - POLL is happening
            last_trans_count = stats.controller_transactions;
            last_activity_time = now;

            // Set LED to POLLING state
            led_set_status(LED_POLLING);
        }
        else
        {
            if (debug_mode)
            {
                // Debug mode: Turn off LED quickly (1ms after last transaction)
                if ((now - last_activity_time) > 1000)
                {
                    if (stats.invalid_transactions > 0 || stats.timeout_errors > 0)
                    {
                        led_set_status(LED_ERROR);
                    }
                    else
                    {
                        led_set_status(LED_READY);
                    }
                }
            }
            else
            {
                // Non-debug mode: Change state after 1 second of inactivity
                if ((now - last_activity_time) > 1000000)
                {
                    if (stats.invalid_transactions > 0 || stats.timeout_errors > 0)
                    {
                        if (current_led_status != LED_ERROR)
                        {
                            led_set_status(LED_ERROR);
                        }
                    }
                    else
                    {
                        if (current_led_status != LED_READY)
                        {
                            led_set_status(LED_READY);
                        }
                    }
                }
            }
        }

        led_update();

        // Debug output every 2 seconds
        if (debug_mode)
        {
            static uint32_t stats_print_count = 0;
            if ((now - last_stats_print) > 2000000)
            {
                stats_print_count++;
                printf("\n=== Stats #%lu ===\n", stats_print_count);
                printf("Total Trans:  %llu\n", stats.total_transactions);
                printf("Controller:   %llu\n", stats.controller_transactions);
                printf("MemCard:      %llu\n", stats.memcard_transactions);
                printf("Invalid:      %llu\n", stats.invalid_transactions);
                printf("Timeout:      %llu\n", stats.timeout_errors);
                if (stats.invalid_transactions > 0)
                {
                    printf("Last Invalid Addr: 0x%02X, Cmd: 0x%02X\n", stats.last_invalid_addr, stats.last_invalid_cmd);
                }

#if ACK_AUTO_TUNE_ENABLED
                // ACK auto-tuning status
                extern uint32_t psx_ack_get_pulse_width(void);
                extern uint32_t psx_ack_get_post_wait(void);
                extern bool psx_ack_is_tuning_complete(void);
                extern bool psx_ack_is_tuning_started(void);

                const char *status;
                if (psx_ack_is_tuning_complete())
                {
                    status = "LOCKED";
                }
                else if (psx_ack_is_tuning_started())
                {
                    status = "tuning...";
                }
                else
                {
                    status = "waiting...";
                }
                printf("ACK:          PULSE=%lu us, WAIT=%lu us (%s)\n",
                       psx_ack_get_pulse_width(), psx_ack_get_post_wait(), status);
#endif

                // Transaction interval statistics
                if (stats.controller_transactions > 0)
                {
                    printf("PSX Interval (us): Min=%lu, Max=%lu, Avg=%lu\n",
                           stats.min_interval_us, stats.max_interval_us, stats.avg_interval_us);
                    printf("PSX Polling Rate:  %.2f Hz\n", 1000000.0f / stats.avg_interval_us);
                }

                // Button sampling statistics
                printf("BTN Target Rate:   %.2f Hz (%lu us)\n",
                       1000000.0f / BUTTON_POLL_INTERVAL_US, (uint32_t)BUTTON_POLL_INTERVAL_US);
                if (sample_count > 0)
                {
                    uint32_t avg_sample_interval = (uint32_t)(total_sample_interval / sample_count);
                    printf("BTN Interval (us): Min=%lu, Max=%lu, Avg=%lu\n",
                           min_sample_interval, max_sample_interval, avg_sample_interval);
                    printf("BTN Sample Rate:   %.2f Hz (actual)\n", 1000000.0f / avg_sample_interval);
                }

                printf("Buttons:      0x%02X 0x%02X\n", btn1, btn2);

                // Show individual button states
                printf("Pressed: ");
                if (!(btn1 & 0x01))
                    printf("SELECT ");
                if (!(btn1 & 0x08))
                    printf("START ");
                if (!(btn1 & 0x10))
                    printf("UP ");
                if (!(btn1 & 0x20))
                    printf("RIGHT ");
                if (!(btn1 & 0x40))
                    printf("DOWN ");
                if (!(btn1 & 0x80))
                    printf("LEFT ");
                if (!(btn2 & 0x01))
                    printf("L2 ");
                if (!(btn2 & 0x02))
                    printf("R2 ");
                if (!(btn2 & 0x04))
                    printf("L1 ");
                if (!(btn2 & 0x08))
                    printf("R1 ");
                if (!(btn2 & 0x10))
                    printf("△ ");
                if (!(btn2 & 0x20))
                    printf("○ ");
                if (!(btn2 & 0x40))
                    printf("☓ ");
                if (!(btn2 & 0x80))
                    printf("□ ");
                printf("\n");

                // Reset interval statistics for next period
                psx_reset_interval_stats();
                sample_count = 0;
                min_sample_interval = 0;
                max_sample_interval = 0;
                total_sample_interval = 0;

                last_stats_print = now;
            }
        }
    }

    return 0;
}
