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

#ifndef GPIO_INPUT_H
#define GPIO_INPUT_H

#include <stdint.h>

// Initialize GPIO pins for direct button input (internal pull-up, active LOW).
void gpio_input_init(void);

// Read all GPIO buttons and return PSX-format button bytes.
// Each bit: 0 = pressed, 1 = released.
void gpio_input_read(uint8_t *btn1, uint8_t *btn2);

#endif // GPIO_INPUT_H
