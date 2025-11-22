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

#include "psx_bitbang.h"
#include "config.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// ACK Auto-Tuning State
// ============================================================================

#if ACK_AUTO_TUNE_ENABLED
static volatile uint32_t current_ack_pulse_width = ACK_PULSE_WIDTH_MAX;
static volatile uint32_t current_ack_post_wait = ACK_POST_WAIT_MIN; // Start from MIN (short WAIT first)
static volatile uint32_t test_start_time = 0;
static volatile uint32_t test_addr_count = 0;  // Address bytes received
static volatile uint32_t test_cmd_success = 0; // Command bytes successfully received (not 0xFF)
static volatile uint32_t best_pulse_width = ACK_PULSE_WIDTH_MAX;
static volatile uint32_t best_post_wait = ACK_POST_WAIT_MIN;
static volatile float best_cmd_success_rate = -1.0f; // Initialize to -1 so first valid result is always "NEW BEST"
static volatile bool tuning_complete = false;
static volatile bool tuning_started = false;        // Track if tuning has started
static volatile uint32_t last_transaction_time = 0; // Time of last transaction

void psx_ack_tune_on_address(void)
{
    uint32_t now = time_us_32();

    // Check for idle timeout - reset if no transaction for a while
    if (last_transaction_time != 0 && (now - last_transaction_time) > ACK_TUNE_IDLE_TIMEOUT_US)
    {
        if (tuning_complete || tuning_started)
        {
            printf("[ACK-TUNE] Idle timeout, resetting...\n");
        }
        psx_ack_tune_reset();
        // Fall through to process this transaction as the first one after reset
    }

    // Update last transaction time
    last_transaction_time = now;

    if (tuning_complete)
    {
        return;
    }

    // Start tuning on first transaction
    if (!tuning_started)
    {
        tuning_started = true;
        printf("[ACK-TUNE] Starting auto-tune...\n");
    }

    test_addr_count++;

    if (test_start_time == 0)
    {
        test_start_time = now;
        // Silent - don't print for every test to avoid timing issues
        return;
    }
}

void psx_ack_tune_on_command(bool cmd_success)
{
    if (tuning_complete)
    {
        return;
    }

    // Don't process until tuning has started
    if (!tuning_started)
    {
        return;
    }

    if (cmd_success)
    {
        test_cmd_success++;
    }

    uint32_t now = time_us_32();
    uint32_t elapsed = now - test_start_time;

    // Test each setting for ACK_TUNE_TEST_TRANSACTIONS transactions
    // OR timeout after ACK_TUNE_TIMEOUT_US
    if (test_addr_count >= ACK_TUNE_TEST_TRANSACTIONS || elapsed >= ACK_TUNE_TIMEOUT_US)
    {
        float cmd_success_rate = (float)test_cmd_success / (float)test_addr_count;

        // Only print when finding a new best to minimize timing impact
        if (cmd_success_rate >= ACK_TUNE_CMD_SUCCESS_THRESHOLD)
        {
            bool is_better = false;

            // Better if higher success rate
            if (cmd_success_rate > best_cmd_success_rate)
            {
                is_better = true;
            }
            // If same success rate, prefer: 1) shorter WAIT, 2) middle PULSE
            else if (cmd_success_rate == best_cmd_success_rate && best_cmd_success_rate >= 0.0f)
            {
                // Calculate distance from middle PULSE value
                uint32_t pulse_mid = (ACK_PULSE_WIDTH_MIN + ACK_PULSE_WIDTH_MAX) / 2;
                int32_t current_pulse_dist = abs((int32_t)current_ack_pulse_width - (int32_t)pulse_mid);
                int32_t best_pulse_dist = abs((int32_t)best_pulse_width - (int32_t)pulse_mid);

                // Prefer shorter WAIT first
                if (current_ack_post_wait < best_post_wait)
                {
                    is_better = true;
                }
                // If same WAIT, prefer middle PULSE
                else if (current_ack_post_wait == best_post_wait && current_pulse_dist < best_pulse_dist)
                {
                    is_better = true;
                }
            }

            if (is_better)
            {
                best_cmd_success_rate = cmd_success_rate;
                best_pulse_width = current_ack_pulse_width;
                best_post_wait = current_ack_post_wait;
                printf("[ACK-TUNE] New best: PULSE=%lu, WAIT=%lu (%.1f%%, %lu/%lu)\n",
                       current_ack_pulse_width, current_ack_post_wait,
                       cmd_success_rate * 100.0f, test_cmd_success, test_addr_count);
            }
        }

        // Move to next setting: WAIT from small to large, PULSE from large to small
        bool moved = false;

        // Try next wait time (increasing from MIN to MAX)
        if (current_ack_post_wait < ACK_POST_WAIT_MAX)
        {
            current_ack_post_wait += ACK_POST_WAIT_STEP;
            moved = true;
        }
        else
        {
            // Reset wait to min, try next pulse width (decreasing)
            current_ack_post_wait = ACK_POST_WAIT_MIN;
            if (current_ack_pulse_width > ACK_PULSE_WIDTH_MIN)
            {
                current_ack_pulse_width -= ACK_PULSE_WIDTH_STEP;
                moved = true;
            }
        }

        if (moved)
        {
            // Continue testing
            test_start_time = 0;
            test_addr_count = 0;
            test_cmd_success = 0;
        }
        else
        {
            // Finished testing all combinations
            if (best_cmd_success_rate >= ACK_TUNE_CMD_SUCCESS_THRESHOLD)
            {
                current_ack_pulse_width = best_pulse_width;
                current_ack_post_wait = best_post_wait;
                tuning_complete = true;
                printf("[ACK-TUNE] LOCKED: PULSE=%lu us, WAIT=%lu us (%.0f%%)\n",
                       best_pulse_width, best_post_wait, best_cmd_success_rate * 100.0f);
            }
            else
            {
                printf("[ACK-TUNE] No good settings, restarting...\n");
                current_ack_pulse_width = ACK_PULSE_WIDTH_MAX;
                current_ack_post_wait = ACK_POST_WAIT_MIN; // Start from MIN
                test_start_time = 0;
                test_addr_count = 0;
                test_cmd_success = 0;
                best_cmd_success_rate = -1.0f; // Reset to -1
            }
        }
    }
}

void psx_ack_tune_reset(void)
{
    current_ack_pulse_width = ACK_PULSE_WIDTH_MAX;
    current_ack_post_wait = ACK_POST_WAIT_MIN; // Start from MIN
    test_start_time = 0;
    test_addr_count = 0;
    test_cmd_success = 0;
    best_pulse_width = ACK_PULSE_WIDTH_MAX;
    best_post_wait = ACK_POST_WAIT_MAX;
    best_cmd_success_rate = -1.0f;
    tuning_complete = false;
    tuning_started = false;
    last_transaction_time = 0;
}

uint32_t psx_ack_get_pulse_width(void)
{
    return current_ack_pulse_width;
}

uint32_t psx_ack_get_post_wait(void)
{
    return current_ack_post_wait;
}

bool psx_ack_is_tuning_complete(void)
{
    return tuning_complete;
}

bool psx_ack_is_tuning_started(void)
{
    return tuning_started;
}
#endif

// Direct SIO register access for reliable open-drain control
// Using pico SDK structures for safer access
inline void gpio_out_low(uint gpio)
{
    // Ensure output register is LOW before enabling output
    gpio_put(gpio, 0);
    __dmb(); // Memory barrier to ensure write completes
    // Now enable output (this drives the pin LOW via open-drain)
    gpio_set_dir(gpio, GPIO_OUT);
    __dmb(); // Memory barrier
}

inline void gpio_hi_z(uint gpio)
{
    // Disable output (release to external pull-up)
    gpio_set_dir(gpio, GPIO_IN);
    __dmb(); // Memory barrier
}

// ============================================================================
// PSX Bit-Banging Implementation
// ============================================================================

void psx_bitbang_init(void)
{
    // CRITICAL: Ensure GPIO function is set to SIO (GPIO mode) BEFORE using gpio_init
    // This prevents conflicts with UART or other peripherals
    gpio_set_function(PIN_DAT, GPIO_FUNC_SIO);
    gpio_set_function(PIN_ACK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_CMD, GPIO_FUNC_SIO);
    gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_SEL, GPIO_FUNC_SIO);

    // Initialize DAT pin (open-drain, bidirectional)
    gpio_init(PIN_DAT);
    gpio_put(PIN_DAT, 0);           // Set output register to LOW FIRST
    gpio_disable_pulls(PIN_DAT);    // NO internal pull-up - rely on external pull-up
    gpio_set_dir(PIN_DAT, GPIO_IN); // Start in Hi-Z state

    // Initialize ACK pin (open-drain output)
    // CRITICAL: ACK must NOT have internal pull-up!
    // The PSX has external pull-up on the bus.
    // Internal pull-up may prevent ACK from going LOW.
    gpio_init(PIN_ACK);
    gpio_put(PIN_ACK, 0);           // Set output register to LOW FIRST
    gpio_disable_pulls(PIN_ACK);    // NO internal pull-up - PSX has external pull-up
    gpio_set_dir(PIN_ACK, GPIO_IN); // Start in Hi-Z state

    // Initialize CMD pin (input from PSX)
    gpio_init(PIN_CMD);
    gpio_disable_pulls(PIN_CMD); // No pull - PSX has external pull-up
    gpio_set_dir(PIN_CMD, GPIO_IN);

    // Initialize CLK pin (input from PSX)
    gpio_init(PIN_CLK);
    gpio_disable_pulls(PIN_CLK); // No pull - PSX drives this line
    gpio_set_dir(PIN_CLK, GPIO_IN);

    // Initialize SEL pin (input from PSX, active LOW)
    gpio_init(PIN_SEL);
    gpio_disable_pulls(PIN_SEL); // No pull - PSX drives this line
    gpio_set_dir(PIN_SEL, GPIO_IN);
}

// ============================================================================
// Open-Drain Control Functions
// ============================================================================

inline void psx_dat_hiz(void)
{
    gpio_set_dir(PIN_DAT, GPIO_IN); // Hi-Z (pulled HIGH externally)
}

inline void psx_dat_low(void)
{
    gpio_set_dir(PIN_DAT, GPIO_OUT); // Drive LOW
}

inline void psx_ack_hiz(void)
{
    gpio_set_dir(PIN_ACK, GPIO_IN); // Hi-Z (pulled HIGH externally)
}

inline void psx_ack_low(void)
{
    gpio_set_dir(PIN_ACK, GPIO_OUT); // Drive LOW
}

// ============================================================================
// Bus Line Reading Functions
// ============================================================================

inline bool psx_read_sel(void)
{
    return gpio_get(PIN_SEL);
}

inline bool psx_read_clk(void)
{
    return gpio_get(PIN_CLK);
}

inline bool psx_read_cmd(void)
{
    return gpio_get(PIN_CMD);
}

// ============================================================================
// Clock Edge Detection with Timeout
// ============================================================================

bool __time_critical_func(psx_wait_clk_rising)(uint32_t timeout_us)
{
    uint32_t start = time_us_32();

    // Wait for CLK to go HIGH
    while (!gpio_get(PIN_CLK))
    {
        // Check for timeout
        if ((time_us_32() - start) > timeout_us)
        {
            return false;
        }
        // Check if SELECT went HIGH (transaction aborted)
        if (gpio_get(PIN_SEL))
        {
            return false;
        }
    }

    return true;
}

bool __time_critical_func(psx_wait_clk_falling)(uint32_t timeout_us)
{
    uint32_t start = time_us_32();

    // Wait for CLK to go LOW
    while (gpio_get(PIN_CLK))
    {
        // Check for timeout
        if ((time_us_32() - start) > timeout_us)
        {
            return false;
        }
        // Check if SELECT went HIGH (transaction aborted)
        if (gpio_get(PIN_SEL))
        {
            return false;
        }
    }

    return true;
}

// ============================================================================
// Byte-Level Communication
// ============================================================================

uint8_t __time_critical_func(psx_receive_byte)(void)
{
    uint8_t data = 0;

    // Receive 8 bits, LSB first
    for (int bit = 0; bit < 8; bit++)
    {
        // Wait for CLK falling edge (PSX outputs data on falling edge)
        if (!psx_wait_clk_falling(PSX_CLK_TIMEOUT_US))
        {
            return 0xFF; // Timeout or abort
        }

        // Wait for CLK rising edge (sample point)
        if (!psx_wait_clk_rising(PSX_CLK_TIMEOUT_US))
        {
            return 0xFF; // Timeout or abort
        }

        // Sample CMD line on rising edge
        if (gpio_get(PIN_CMD))
        {
            data |= (1 << bit);
        }
    }

    return data;
}

bool __time_critical_func(psx_send_byte)(uint8_t data)
{
    // Send 8 bits, LSB first
    for (int bit = 0; bit < 8; bit++)
    {
        // Wait for CLK falling edge (output data)
        if (!psx_wait_clk_falling(PSX_CLK_TIMEOUT_US))
        {
            // Ensure DAT is Hi-Z before returning
            gpio_set_dir(PIN_DAT, GPIO_IN);
            return false; // Timeout or abort
        }

        // Set DAT line according to current bit immediately after falling edge
        if (data & (1 << bit))
        {
            gpio_set_dir(PIN_DAT, GPIO_IN); // Hi-Z = 1
        }
        else
        {
            gpio_set_dir(PIN_DAT, GPIO_OUT); // LOW = 0
        }

        // Wait for CLK rising edge (PSX samples data)
        if (!psx_wait_clk_rising(PSX_CLK_TIMEOUT_US))
        {
            // Ensure DAT is Hi-Z before returning
            gpio_set_dir(PIN_DAT, GPIO_IN);
            return false; // Timeout or abort
        }
    }

    // After byte is sent, ensure DAT returns to Hi-Z (idle state)
    gpio_set_dir(PIN_DAT, GPIO_IN);

    return true;
}

// Simultaneous send and receive (full duplex)
uint8_t __time_critical_func(psx_transfer_byte)(uint8_t data_out)
{
    uint8_t data_in = 0;

    // Transfer 8 bits, LSB first
    for (int bit = 0; bit < 8; bit++)
    {
        // Wait for CLK falling edge
        if (!psx_wait_clk_falling(PSX_CLK_TIMEOUT_US))
        {
            return 0xFF; // Timeout or abort
        }

        // Sample input data on CMD line immediately after falling edge
        bool cmd_bit = gpio_get(PIN_CMD);

        // Output data on DAT line
        if (data_out & (1 << bit))
        {
            gpio_set_dir(PIN_DAT, GPIO_IN); // Hi-Z = 1
        }
        else
        {
            gpio_set_dir(PIN_DAT, GPIO_OUT); // LOW = 0
        }

        // Wait for CLK rising edge
        if (!psx_wait_clk_rising(PSX_CLK_TIMEOUT_US))
        {
            // Ensure DAT is Hi-Z before returning
            gpio_set_dir(PIN_DAT, GPIO_IN);
            return 0xFF; // Timeout or abort
        }

        // Use the sampled bit
        if (cmd_bit)
        {
            data_in |= (1 << bit);
        }
    }

    // After byte is transferred, ensure DAT returns to Hi-Z (idle state)
    gpio_set_dir(PIN_DAT, GPIO_IN);

    return data_in;
}

void __time_critical_func(psx_send_ack)(void)
{
#if ACK_AUTO_TUNE_ENABLED
    // busy_wait_us_32(current_ack_post_wait);
    busy_wait_us_32(5);
#else
    busy_wait_us_32(ACK_PULSE_WIDTH_US);
#endif
    // Assert ACK (drive LOW) immediately after byte transfer
    gpio_out_low(PIN_ACK);

    // Hold ACK for specified duration (auto-tuned or fixed)
#if ACK_AUTO_TUNE_ENABLED
    busy_wait_us_32(current_ack_pulse_width);
#else
    busy_wait_us_32(ACK_PULSE_WIDTH_US);
#endif

    // Release ACK (Hi-Z)
    gpio_hi_z(PIN_ACK);
}

// ============================================================================
// Bus Release
// ============================================================================

inline void psx_release_bus(void)
{
    // Release both DAT and ACK to Hi-Z
    gpio_set_dir(PIN_DAT, GPIO_IN);
    gpio_set_dir(PIN_ACK, GPIO_IN);
}
