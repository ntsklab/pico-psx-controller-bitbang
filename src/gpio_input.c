/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * GPIO Direct Button Input
 * Copyright (C) 2024-2025 ntsklab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "gpio_input.h"
#include "config.h"
#include "hardware/gpio.h"

// Button-to-pin mapping table.
// Each entry: { gpio_pin, byte_index (0=btn1, 1=btn2), bit_position }
typedef struct {
    uint8_t gpio;
    uint8_t byte_idx; // 0 = btn1, 1 = btn2
    uint8_t bit;
} gpio_btn_map_t;

static const gpio_btn_map_t g_btn_map[GPIO_BTN_COUNT] = {
    // byte1: SELECT(0), L3(1), R3(2), START(3), UP(4), RIGHT(5), DOWN(6), LEFT(7)
    { GPIO_BTN_SELECT,   0, 0 },
    { GPIO_BTN_START,    0, 3 },
    { GPIO_BTN_UP,       0, 4 },
    { GPIO_BTN_RIGHT,    0, 5 },
    { GPIO_BTN_DOWN,     0, 6 },
    { GPIO_BTN_LEFT,     0, 7 },
    // byte2: L2(0), R2(1), L1(2), R1(3), TRIANGLE(4), CIRCLE(5), CROSS(6), SQUARE(7)
    { GPIO_BTN_L2,       1, 0 },
    { GPIO_BTN_R2,       1, 1 },
    { GPIO_BTN_L1,       1, 2 },
    { GPIO_BTN_R1,       1, 3 },
    { GPIO_BTN_TRIANGLE, 1, 4 },
    { GPIO_BTN_CIRCLE,   1, 5 },
    { GPIO_BTN_CROSS,    1, 6 },
    { GPIO_BTN_SQUARE,   1, 7 },
};

void gpio_input_init(void)
{
    // Ensure ADC pins (GP26-28) are configured as digital GPIO, not ADC.
    // On RP2040, gpio_init() overrides ADC function to SIO.
    for (uint8_t i = 0; i < GPIO_BTN_COUNT; i++)
    {
        uint8_t pin = g_btn_map[i].gpio;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }
}

void gpio_input_read(uint8_t *btn1, uint8_t *btn2)
{
    uint8_t b1 = 0xFF; // all released
    uint8_t b2 = 0xFF;

    for (uint8_t i = 0; i < GPIO_BTN_COUNT; i++)
    {
        if (!gpio_get(g_btn_map[i].gpio)) // active LOW = pressed
        {
            if (g_btn_map[i].byte_idx == 0)
            {
                b1 &= ~(1u << g_btn_map[i].bit);
            }
            else
            {
                b2 &= ~(1u << g_btn_map[i].bit);
            }
        }
    }

    *btn1 = b1;
    *btn2 = b2;
}
