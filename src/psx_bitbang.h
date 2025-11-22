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

#ifndef PSX_BITBANG_H
#define PSX_BITBANG_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// ============================================================================
// PSX Bit-Banging Low-Level Functions
// ============================================================================

// Initialize PSX bus GPIO pins
void psx_bitbang_init(void);

// Open-drain control functions for DAT and ACK lines
// Hi-Z state: set pin to input mode (pulled HIGH by external resistor)
// LOW state: set pin to output mode with LOW value
void psx_dat_hiz(void);     // Release DAT line (Hi-Z)
void psx_dat_low(void);     // Assert DAT line LOW
void psx_ack_hiz(void);     // Release ACK line (Hi-Z)
void psx_ack_low(void);     // Assert ACK line LOW

// Read current state of bus lines
bool psx_read_sel(void);    // Read SELECT line (true = HIGH/inactive)
bool psx_read_clk(void);    // Read CLOCK line
bool psx_read_cmd(void);    // Read COMMAND line

// Wait for clock edges with timeout
// Returns true if edge detected, false if timeout
bool psx_wait_clk_rising(uint32_t timeout_us);
bool psx_wait_clk_falling(uint32_t timeout_us);

// Receive one byte from PSX (read CMD line on CLK rising edges)
// Returns received byte, or 0xFF on timeout
uint8_t psx_receive_byte(void);

// Send one byte to PSX (set DAT line on CLK falling edges)
// Returns true if successful, false if timeout
bool psx_send_byte(uint8_t data);

// Send ACK pulse after byte transmission
// Asserts ACK LOW for ACK_PULSE_WIDTH_US after ACK_DELAY_US
void psx_send_ack(void);

// Simultaneous send and receive (full duplex)
// Sends data_out while receiving and returning data_in
uint8_t psx_transfer_byte(uint8_t data_out);

// Release bus completely (both DAT and ACK to Hi-Z)
void psx_release_bus(void);

// ============================================================================
// ACK Auto-Tuning Functions (only available if ACK_AUTO_TUNE_ENABLED)
// ============================================================================

#if ACK_AUTO_TUNE_ENABLED
// Call when address byte is received (increments attempt counter)
void psx_ack_tune_on_address(void);

// Call when command byte is received (cmd_success = cmd != 0xFF)
void psx_ack_tune_on_command(bool cmd_success);

// Reset tuning state
void psx_ack_tune_reset(void);

// Get current ACK pulse width
uint32_t psx_ack_get_pulse_width(void);

// Get current ACK post-wait time
uint32_t psx_ack_get_post_wait(void);

// Check if tuning is complete
bool psx_ack_is_tuning_complete(void);

// Check if tuning has started
bool psx_ack_is_tuning_started(void);
#endif

#endif // PSX_BITBANG_H
