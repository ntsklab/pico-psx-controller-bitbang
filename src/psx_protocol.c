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

#include "psx_protocol.h"
#include "psx_bitbang.h"
#include "config.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// Protocol State and Statistics
// ============================================================================

static volatile bool transaction_active = false;
static psx_stats_t stats = {0};
static uint32_t last_transaction_time = 0;
static uint64_t total_interval_sum = 0;
static uint64_t interval_count = 0;

// ============================================================================
// Forward Declarations
// ============================================================================

static bool handle_poll_command(uint8_t btn1, uint8_t btn2);

// ============================================================================
// Initialization
// ============================================================================

void psx_protocol_init(void)
{
    // Initialize bit-banging layer
    psx_bitbang_init();

    // Set up SELECT interrupt for rising edge (transaction end/abort)
    gpio_set_irq_enabled_with_callback(PIN_SEL, GPIO_IRQ_EDGE_RISE, true,
                                       &psx_sel_interrupt_handler);

    // Reset statistics
    psx_reset_stats();

    transaction_active = false;
}

// ============================================================================
// SELECT Interrupt Handler
// ============================================================================

void __time_critical_func(psx_sel_interrupt_handler)(unsigned int gpio_num, uint32_t events)
{
    // Acknowledge interrupt
    gpio_acknowledge_irq(PIN_SEL, GPIO_IRQ_EDGE_RISE);

    // Immediately release bus on SELECT rising edge
    psx_release_bus();

    // Mark transaction as inactive
    transaction_active = false;
}

// ============================================================================
// Main Protocol Task (Core 1)
// ============================================================================

void psx_protocol_task(void)
{
    while (1)
    {
        // Wait for SELECT to go LOW (transaction start)
        while (psx_read_sel())
        {
            tight_loop_contents();
        }

        // Small delay to ensure SELECT is stable
        busy_wait_us_32(1);

        // Double-check SELECT is still LOW
        if (psx_read_sel())
        {
            continue; // False trigger, go back to waiting
        }

        // Mark transaction as active
        transaction_active = true;

        // Receive first byte (device address) - don't send anything yet, keep DAT Hi-Z
        uint8_t addr = psx_receive_byte();

        // Check if transaction was aborted
        if (!transaction_active || psx_read_sel())
        {
            psx_release_bus();
            continue;
        }

        // Count all transactions (valid or invalid)
        stats.total_transactions++;

        // CRITICAL: Check for memory card FIRST and immediately release bus
        // This must happen before any other processing to avoid interfering with memory card communication
        if (addr == PSX_ADDR_MEMCARD)
        {
            // Memory card addressed - immediately release bus and stay completely silent
            stats.memcard_transactions++;
            psx_release_bus();

            // IMPORTANT: Wait for the entire memory card transaction to complete
            // SEL will stay LOW during the full memory card communication
            // We must wait until SEL goes HIGH before starting to listen for next transaction
            while (!psx_read_sel() && transaction_active)
            {
                tight_loop_contents();
            }

            // Transaction ended
            transaction_active = false;

            // Skip all further processing for this transaction
            continue;
        }

        // Handle based on address
        if (addr == PSX_ADDR_CONTROLLER)
        {
            // Controller addressed - process transaction
            stats.controller_transactions++;

            // Ensure DAT is Hi-Z before ACK
            gpio_set_dir(PIN_DAT, GPIO_IN);

            // Send ACK after receiving address byte immediately (no debug output here - timing critical!)
            // Disable SEL interrupt temporarily to avoid false abort during ACK pulse
            gpio_set_irq_enabled(PIN_SEL, GPIO_IRQ_EDGE_RISE, false);
            psx_send_ack();
            // Clear any pending interrupts before re-enabling
            gpio_acknowledge_irq(PIN_SEL, GPIO_IRQ_EDGE_RISE);
            gpio_set_irq_enabled(PIN_SEL, GPIO_IRQ_EDGE_RISE, true);

            // Check if SEL went HIGH during ACK
            if (psx_read_sel())
            {
                psx_release_bus();
                continue;
            }

#if ACK_AUTO_TUNE_ENABLED
            // Report address byte received for auto-tuning
            extern void psx_ack_tune_on_address(void);
            psx_ack_tune_on_address();

            // Wait for PSX to prepare for CMD transmission after ACK (auto-tuned)
            extern uint32_t psx_ack_get_post_wait(void);
            busy_wait_us_32(psx_ack_get_post_wait());
#else
            // Fixed wait time
            busy_wait_us_32(50);
#endif

            // Now start responding: receive command byte while sending controller ID low byte
            uint8_t cmd = psx_transfer_byte(PSX_ID_DIGITAL_LO);

#if ACK_AUTO_TUNE_ENABLED
            // Report command byte result for auto-tuning
            extern void psx_ack_tune_on_command(bool cmd_success);
            psx_ack_tune_on_command(cmd != 0xFF);
#endif

            // Disable SEL interrupt briefly - no debug output here, timing critical!
            gpio_set_irq_enabled(PIN_SEL, GPIO_IRQ_EDGE_RISE, false);

            if (cmd == 0xFF)
            {
                // transfer_byte returned 0xFF = timeout or abort during transfer
                psx_release_bus();
                gpio_set_irq_enabled(PIN_SEL, GPIO_IRQ_EDGE_RISE, true);
                continue;
            }

            // Check SEL state while interrupt disabled
            if (psx_read_sel())
            {
                psx_release_bus();
                gpio_set_irq_enabled(PIN_SEL, GPIO_IRQ_EDGE_RISE, true);
                continue;
            }

            // SEL is still LOW - safe to proceed, now re-enable interrupt
            gpio_set_irq_enabled(PIN_SEL, GPIO_IRQ_EDGE_RISE, true);

            if (!transaction_active || psx_read_sel())
            {
                psx_release_bus();
                continue;
            }

            // Handle command
            if (cmd == PSX_CMD_POLL)
            {
                // Calculate poll interval (only for 0x42 command)
                uint32_t current_time = time_us_32();
                if (last_transaction_time != 0)
                {
                    uint32_t interval = current_time - last_transaction_time;

                    // Update min/max
                    if (stats.min_interval_us == 0 || interval < stats.min_interval_us)
                    {
                        stats.min_interval_us = interval;
                    }
                    if (interval > stats.max_interval_us)
                    {
                        stats.max_interval_us = interval;
                    }

                    // Update average
                    total_interval_sum += interval;
                    interval_count++;
                    stats.avg_interval_us = (uint32_t)(total_interval_sum / interval_count);
                }
                last_transaction_time = current_time;

                // Read current button state from shared memory
                uint8_t btn1, btn2;
                extern void shared_state_read(uint8_t *btn1, uint8_t *btn2);
                shared_state_read(&btn1, &btn2);

                // Process poll command (0x42) - only command we support
                bool success = handle_poll_command(btn1, btn2);
                (void)success; // Suppress unused variable warning
            }
            else
            {
                // Ignore all other commands (Config mode commands, etc.)
                // Digital controller doesn't need to respond to Config mode
                psx_release_bus();
            }
        }
        else
        {
            // Check if this is a known address to ignore
            static const uint8_t ignored_addresses[] = {
                0xFF, // Timeout or aborted address byte
                0x21, // Yaroze Access Card / PS2 multitap (incompatible with PS1)
                0x61, // PS2 DVD remote receiver
                0x43, // Config command address
                0x4D, // Config command address
            };

            bool should_ignore = false;
            for (size_t i = 0; i < sizeof(ignored_addresses); i++)
            {
                if (addr == ignored_addresses[i])
                {
                    should_ignore = true;
                    break;
                }
            }

            if (should_ignore)
            {
                // Known address to ignore - stay silent
                psx_release_bus();
            }
            else
            {
                // Truly unknown address - log it
                stats.invalid_transactions++;
                stats.last_invalid_addr = addr;
                psx_release_bus();
            }
        }

        // Ensure bus is released at end of transaction
        psx_release_bus();
        transaction_active = false;
    }
}

// ============================================================================
// Command Handlers
// ============================================================================

static bool __time_critical_func(handle_poll_command)(uint8_t btn1, uint8_t btn2)
{
    // Poll command sequence:
    // PSX -> Controller:  0x01  0x42  0x00  0x00  0x00
    // Controller -> PSX:  0xFF  0x41  0x5A  btn1  btn2

    // We've already transferred: 0x01/0xFF and 0x42/0x41
    // Send ACK after ID_LO
    psx_send_ack();

    if (!transaction_active || psx_read_sel())
    {
        return false;
    }

    // Transfer: receive 0x00, send ID_HI (0x5A)
    uint8_t dummy1 = psx_transfer_byte(PSX_ID_DIGITAL_HI);
    if (!transaction_active || psx_read_sel())
    {
        return false;
    }

    psx_send_ack();
    if (!transaction_active || psx_read_sel())
    {
        return false;
    }

    // Transfer: receive 0x00, send button data byte 1
    uint8_t dummy2 = psx_transfer_byte(btn1);
    if (!transaction_active || psx_read_sel())
    {
        return false;
    }
    psx_send_ack();

    if (!transaction_active || psx_read_sel())
    {
        return false;
    }

    // Transfer: receive 0x00, send button data byte 2
    uint8_t dummy3 = psx_transfer_byte(btn2);
    if (!transaction_active || psx_read_sel())
    {
        return false;
    }
    // NOTE: Do NOT send ACK after the last byte!
    // Spec: "Once the last byte of the packet is transferred,
    //        the device shall no longer pulse /ACK."

    // Transaction complete
    return true;
}

// ============================================================================
// Public API for Transaction Processing
// ============================================================================

bool psx_process_transaction(uint8_t btn1, uint8_t btn2)
{
    // This function is kept for API compatibility
    // The actual processing is done in psx_protocol_task()
    return true;
}

// ============================================================================
// Statistics Functions
// ============================================================================

void psx_get_stats(psx_stats_t *stats_out)
{
    if (stats_out)
    {
        *stats_out = stats;
    }
}

void psx_reset_stats(void)
{
    stats.total_transactions = 0;
    stats.controller_transactions = 0;
    stats.memcard_transactions = 0;
    stats.invalid_transactions = 0;
    stats.timeout_errors = 0;
    stats.min_interval_us = 0;
    stats.max_interval_us = 0;
    stats.avg_interval_us = 0;
    last_transaction_time = 0;
    total_interval_sum = 0;
}

void psx_reset_interval_stats(void)
{
    stats.min_interval_us = 0;
    stats.max_interval_us = 0;
    stats.avg_interval_us = 0;
    // Keep last_transaction_time to maintain continuity
    total_interval_sum = 0;
    interval_count = 0;
}
