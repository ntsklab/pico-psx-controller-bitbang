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

#ifndef CONFIG_H
#define CONFIG_H

#include "pico/stdlib.h"

// ============================================================================
// PSX/PS2 Bus Signal Pin Definitions
// ============================================================================

#define PIN_DAT 3  // Data line (Open-drain, bidirectional)
#define PIN_CMD 4  // Command line (Input from PSX)
#define PIN_SEL 10 // Select/Chip Select (Input from PSX, Active LOW)
#define PIN_CLK 6  // Clock (Input from PSX, ~250kHz)
#define PIN_ACK 7  // Acknowledge (Open-drain output to PSX)

// ============================================================================
// Button Input GPIO Pin Definitions 
// ============================================================================

// Face buttons
#define BTN_CIRCLE 22   // ○
#define BTN_CROSS 21    // ☓
#define BTN_TRIANGLE 20 // △
#define BTN_SQUARE 19   // □

// Shoulder buttons
#define BTN_L1 14
#define BTN_R1 12
#define BTN_L2 13
#define BTN_R2 11

// D-pad
#define BTN_UP 18
#define BTN_DOWN 17
#define BTN_LEFT 16
#define BTN_RIGHT 15

// System buttons
#define BTN_START 26
#define BTN_SELECT 27

// ============================================================================
// Status LED
// ============================================================================

#define LED_PIN PICO_DEFAULT_LED_PIN // GPIO 25

// ============================================================================
// Timing Constants
// ============================================================================

// PSX bus timing
#define PSX_CLOCK_FREQ_HZ 250000 // ~250kHz typical
#define PSX_BIT_PERIOD_US 4      // ~4μs per bit at 250kHz
#define PSX_BYTE_TIMEOUT_US 200  // Timeout for byte reception
#define PSX_CLK_TIMEOUT_US 200   // Timeout for individual clock edge - increased from 50µs

// ============================================================================
// ACK Timing Configuration
// ============================================================================

// ACK Auto-Tuning
// Automatically adjusts ACK pulse width and post-wait timing
// to find optimal parameters for both PS1 and PS2 compatibility
#define ACK_AUTO_TUNE_ENABLED 1

#if ACK_AUTO_TUNE_ENABLED
// Auto-tuning parameter ranges - tested for PS1/PS2 compatibility
#define ACK_PULSE_WIDTH_MIN 1  // Minimum pulse width (1µs for PS2 high-speed)
#define ACK_PULSE_WIDTH_MAX 6  // Maximum pulse width (6µs for PS1 compatibility)
#define ACK_PULSE_WIDTH_STEP 1 // Pulse width increment step (1µs)

#define ACK_POST_WAIT_MIN 0  // Minimum wait after ACK (0µs)
#define ACK_POST_WAIT_MAX 6  // Maximum wait after ACK (6µs)
#define ACK_POST_WAIT_STEP 1 // Wait time increment step (1µs)

// Auto-tuning behavior
#define ACK_TUNE_TEST_TRANSACTIONS 8       // Number of transactions to test each setting
#define ACK_TUNE_TIMEOUT_US 10000000       // Max wait time per setting (10 seconds)
#define ACK_TUNE_CMD_SUCCESS_THRESHOLD 0.5 // Command success rate threshold (50%)
#define ACK_TUNE_IDLE_TIMEOUT_US 5000000   // Reset tuning if no transaction for 5 seconds
#else
// Fixed ACK timing (when auto-tune is disabled)
#define ACK_PULSE_WIDTH_US 3 // ACK pulse width (3µs)
#define ACK_POST_WAIT_US 50  // Wait after ACK (50µs)
#endif

// ============================================================================
// Button Polling Configuration
// ============================================================================

#define BUTTON_POLL_INTERVAL_US 1000 // Button sampling rate: 1000µs = 1kHz

// Button input mode
// 0: Direct mode - PSX reads current button state (may miss brief inputs)
// 1: Latching mode - Button presses are held until PSX reads them (guarantees detection)
#define BUTTON_LATCHING_MODE 1

// ============================================================================
// PSX Protocol Constants
// ============================================================================

// Device addresses
#define PSX_ADDR_CONTROLLER 0x01
#define PSX_ADDR_MEMCARD 0x81

// Commands
#define PSX_CMD_POLL 0x42        // Poll controller
#define PSX_CMD_CONFIG_MODE 0x43 // Enter/exit config mode
#define PSX_CMD_SET_ANALOG 0x44  // Set analog mode
#define PSX_CMD_GET_STATUS 0x45  // Get controller status

// Controller IDs
#define PSX_ID_DIGITAL_LO 0x41 // Digital controller ID low byte
#define PSX_ID_DIGITAL_HI 0x5A // Digital controller ID high byte
#define PSX_ID_ANALOG_LO 0x73  // Analog controller ID low byte

// Response bytes
#define PSX_RESPONSE_IDLE 0xFF // Default Hi-Z state
#define PSX_RESPONSE_NONE 0xFF // No response

// Protocol lengths
#define PSX_DIGITAL_RESPONSE_LEN 5 // Total bytes in digital response

// ============================================================================
// Debug Configuration
// ============================================================================

#define DEBUG_ENABLED 0 // Default debug mode (can be toggled at runtime)

// ============================================================================
// LED Status Modes
// ============================================================================

typedef enum
{
    LED_READY,         // System ready (1 blink pattern in non-debug mode)
    LED_POLLING,       // Controller polling active (2 blink pattern in non-debug mode)
    LED_ERROR,         // Error condition (3 blink pattern in non-debug mode)
    LED_IDLE,          // Deprecated - use LED_READY
    LED_ACTIVE,        // Deprecated - use LED_POLLING
    LED_MEMCARD_DETECT // Memory card access detected
} led_status_t;

#endif // CONFIG_H
