/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * XInput Host Driver Bridge
 * Copyright (C) 2024-2025 ntsklab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef XINPUT_HOST_DRIVER_H
#define XINPUT_HOST_DRIVER_H

#include <stdbool.h>

// Called from the main loop to start the first XInput transfer after enumeration.
bool xinput_receive_report(void);

#endif // XINPUT_HOST_DRIVER_H
