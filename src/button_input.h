#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include <stdint.h>

// ============================================================================
// Button Input Management
// ============================================================================

// Initialize all button GPIO pins
void button_input_init(void);

// Read all buttons and return PSX format byte 1
// bit 0 = SELECT
// bit 1 = L3 (not used in digital mode, always 1)
// bit 2 = R3 (not used in digital mode, always 1)
// bit 3 = START
// bit 4 = UP
// bit 5 = RIGHT
// bit 6 = DOWN
// bit 7 = LEFT
uint8_t button_read_byte1(void);

// Read all buttons and return PSX format byte 2
// bit 0 = L2
// bit 1 = R2
// bit 2 = L1
// bit 3 = R1
// bit 4 = Triangle
// bit 5 = Circle
// bit 6 = Cross
// bit 7 = Square
uint8_t button_read_byte2(void);

#endif // BUTTON_INPUT_H
