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

shared_controller_state_t g_shared_state[SHARED_STATE_NUM_PORTS];

// Latching mode: per-port latched button state
static uint8_t latched_btn1[SHARED_STATE_NUM_PORTS] = { 0xFF, 0xFF };
static uint8_t latched_btn2[SHARED_STATE_NUM_PORTS] = { 0xFF, 0xFF };

// External runtime configuration
extern bool latching_mode;

// ============================================================================
// Implementation
// ============================================================================

void shared_state_init(void)
{
    for (uint8_t port = 0; port < SHARED_STATE_NUM_PORTS; port++)
    {
        for (int i = 0; i < 2; i++)
        {
            g_shared_state[port].buffer[i].buttons1 = 0xFF;
            g_shared_state[port].buffer[i].buttons2 = 0xFF;
        }
        g_shared_state[port].write_index = 0;
        g_shared_state[port].read_index = 0;
        latched_btn1[port] = 0xFF;
        latched_btn2[port] = 0xFF;
    }
}

void shared_state_write(uint8_t port, uint8_t btn1, uint8_t btn2)
{
    if (port >= SHARED_STATE_NUM_PORTS) return;
    shared_controller_state_t* s = &g_shared_state[port];
    uint32_t write_idx = 1 - s->read_index;

    if (latching_mode)
    {
        latched_btn1[port] &= btn1;
        latched_btn2[port] &= btn2;
        s->buffer[write_idx].buttons1 = latched_btn1[port];
        s->buffer[write_idx].buttons2 = latched_btn2[port];
    }
    else
    {
        s->buffer[write_idx].buttons1 = btn1;
        s->buffer[write_idx].buttons2 = btn2;
    }

    __dmb();
    s->write_index = write_idx;
}

void shared_state_read(uint8_t port, uint8_t *btn1, uint8_t *btn2)
{
    if (port >= SHARED_STATE_NUM_PORTS) { *btn1 = 0xFF; *btn2 = 0xFF; return; }
    shared_controller_state_t* s = &g_shared_state[port];

    uint32_t read_idx = s->write_index;
    s->read_index = read_idx;
    __dmb();

    *btn1 = s->buffer[read_idx].buttons1;
    *btn2 = s->buffer[read_idx].buttons2;

    if (latching_mode)
    {
        latched_btn1[port] = 0xFF;
        latched_btn2[port] = 0xFF;
    }

#if SOCD_CLEANER_ENABLED
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
#endif
}
