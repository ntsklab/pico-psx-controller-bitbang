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

#include "shared_state.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

// ============================================================================
// Global Shared State
// ============================================================================

shared_controller_state_t g_shared_state;

#if BUTTON_LATCHING_MODE
// Latching mode: Store accumulated button presses
static uint8_t latched_btn1 = 0xFF;
static uint8_t latched_btn2 = 0xFF;
#endif

// ============================================================================
// Implementation
// ============================================================================

void shared_state_init(void)
{
    // Initialize both buffers with idle state (all buttons released = 0xFF)
    for (int i = 0; i < 2; i++)
    {
        g_shared_state.buffer[i].buttons1 = 0xFF;
        g_shared_state.buffer[i].buttons2 = 0xFF;
    }

    // Initialize indices
    g_shared_state.write_index = 0;
    g_shared_state.read_index = 0;
}

void shared_state_write(uint8_t btn1, uint8_t btn2)
{
#if BUTTON_LATCHING_MODE
    // Latching mode: Accumulate button presses (0 = pressed)
    // Once a button is pressed (bit = 0), keep it pressed until PSX reads it
    latched_btn1 &= btn1; // Bitwise AND - keeps 0s (pressed buttons)
    latched_btn2 &= btn2;

    // Write latched state to buffer
    uint32_t write_idx = 1 - g_shared_state.read_index;
    g_shared_state.buffer[write_idx].buttons1 = latched_btn1;
    g_shared_state.buffer[write_idx].buttons2 = latched_btn2;
#else
    // Direct mode: Write current button state directly
    uint32_t write_idx = 1 - g_shared_state.read_index;
    g_shared_state.buffer[write_idx].buttons1 = btn1;
    g_shared_state.buffer[write_idx].buttons2 = btn2;
#endif

    // Memory barrier to ensure writes complete before index update
    __dmb();

    // Switch to new buffer
    g_shared_state.write_index = write_idx;
}

void shared_state_read(uint8_t *btn1, uint8_t *btn2)
{
    // Read from the latest complete buffer
    uint32_t read_idx = g_shared_state.write_index;

    // Update read index
    g_shared_state.read_index = read_idx;

    // Memory barrier to ensure index is read before data
    __dmb();

    // Read button state
    *btn1 = g_shared_state.buffer[read_idx].buttons1;
    *btn2 = g_shared_state.buffer[read_idx].buttons2;

#if BUTTON_LATCHING_MODE
    // Clear latched state after PSX reads it
    latched_btn1 = 0xFF;
    latched_btn2 = 0xFF;
#endif

    // ========================================================================
    // SOCD (Simultaneous Opposite Cardinal Direction) Cleaner - HitBox style
    // ========================================================================
    // Button mapping in btn1:
    // Bit 4: UP (0 = pressed)
    // Bit 5: RIGHT (0 = pressed)
    // Bit 6: DOWN (0 = pressed)
    // Bit 7: LEFT (0 = pressed)

    bool up_pressed = !(*btn1 & 0x10);
    bool right_pressed = !(*btn1 & 0x20);
    bool down_pressed = !(*btn1 & 0x40);
    bool left_pressed = !(*btn1 & 0x80);

    // Left + Right = Neutral (both released)
    if (left_pressed && right_pressed)
    {
        *btn1 |= 0x80; // Release LEFT
        *btn1 |= 0x20; // Release RIGHT
    }

    // Up + Down = Neutral (both released)
    if (up_pressed && down_pressed)
    {
        *btn1 |= 0x10; // Release UP
        *btn1 |= 0x40; // Release DOWN
    }
}
