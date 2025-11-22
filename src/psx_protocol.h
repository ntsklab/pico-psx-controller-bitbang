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

#ifndef PSX_PROTOCOL_H
#define PSX_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// PSX Protocol High-Level Functions
// ============================================================================

// Initialize PSX protocol layer
// Sets up SELECT interrupt and initializes state
void psx_protocol_init(void);

// Core 1: Main protocol handler loop
// Waits for SELECT LOW, then processes transaction
void psx_protocol_task(void);

// Process a complete PSX transaction
// Called when SELECT goes LOW
// Returns true if transaction was for controller (0x01)
bool psx_process_transaction(uint8_t btn1, uint8_t btn2);

// SELECT interrupt handler
// Called on SELECT rising edge to abort transaction
void psx_sel_interrupt_handler(unsigned int gpio_num, uint32_t events);

// Get transaction statistics for debugging
typedef struct
{
    uint64_t total_transactions; // 64-bit for long-term operation (5+ billion years)
    uint64_t controller_transactions;
    uint64_t memcard_transactions;
    uint64_t invalid_transactions;
    uint64_t timeout_errors;
    uint8_t last_invalid_addr; // Last invalid address received
    uint8_t last_invalid_cmd;  // Last invalid command received
    uint32_t min_interval_us;  // Minimum transaction interval (microseconds)
    uint32_t max_interval_us;  // Maximum transaction interval (microseconds)
    uint32_t avg_interval_us;  // Average transaction interval (microseconds)
} psx_stats_t;

void psx_get_stats(psx_stats_t *stats);
void psx_reset_stats(void);
void psx_reset_interval_stats(void);

#endif // PSX_PROTOCOL_H
