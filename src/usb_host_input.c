/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * USB Host Input Handler Implementation
 * Copyright (C) 2024-2025 ntsklab
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "usb_host_input.h"
#include "xinput_host_driver.h"

#include "tusb.h"
#include "host/usbh.h"
#include "class/hid/hid_host.h"

typedef enum {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_JOYSTICK,
    DEVICE_TYPE_XINPUT,
    DEVICE_TYPE_KEYBOARD,
} device_type_t;

typedef struct {
    bool connected;
    bool has_analog;
    bool xinput_custom_driver;
    device_type_t type;
    uint8_t addr;
    uint8_t instance;
    uint8_t hid_protocol;
    uint16_t vid;
    uint16_t pid;

    uint8_t button_byte1;
    uint8_t button_byte2;

    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
} usb_device_t;

// Xbox 360 / XInput button bit layout in little-endian button word.
#define XINPUT_DPAD_UP      (1u << 0)
#define XINPUT_DPAD_DOWN    (1u << 1)
#define XINPUT_DPAD_LEFT    (1u << 2)
#define XINPUT_DPAD_RIGHT   (1u << 3)
#define XINPUT_START        (1u << 4)
#define XINPUT_BACK         (1u << 5)
#define XINPUT_L3           (1u << 6)
#define XINPUT_R3           (1u << 7)
#define XINPUT_LB           (1u << 8)
#define XINPUT_RB           (1u << 9)
#define XINPUT_A            (1u << 12)
#define XINPUT_B            (1u << 13)
#define XINPUT_X            (1u << 14)
#define XINPUT_Y            (1u << 15)

#define XINPUT_TRIGGER_THRESHOLD 32u

static usb_device_t g_dev;
static bool g_hid_mounted[CFG_TUH_HID];
static uint8_t g_hid_protocol[CFG_TUH_HID];

static inline uint16_t read_le16(uint8_t const* p)
{
    return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static inline uint32_t read_le32(uint8_t const* p)
{
    return (uint32_t) p[0] |
           ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static inline int16_t read_i16(uint8_t const* p)
{
    return (int16_t) read_le16(p);
}

static inline uint8_t scale_signed16_to_u8(int16_t v)
{
    int32_t mapped = ((int32_t) v + 32768) * 255 / 65535;
    if (mapped < 0) {
        mapped = 0;
    }
    if (mapped > 255) {
        mapped = 255;
    }
    return (uint8_t) mapped;
}

static inline uint8_t scale_u16_to_u8(uint16_t v)
{
    // ArduinoJoystickLibrary commonly uses 0..1023. Fall back to full 16-bit scaling.
    if (v <= 1023u) {
        return (uint8_t) ((v * 255u) / 1023u);
    }
    return (uint8_t) (v >> 8);
}

static inline void psx_set_pressed(uint8_t* b, uint8_t bit, bool pressed)
{
    if (pressed) {
        *b &= (uint8_t) ~(1u << bit);
    } else {
        *b |= (uint8_t) (1u << bit);
    }
}

static inline bool is_arduino_vid(uint16_t vid)
{
    return (vid == 0x2341u || vid == 0x2A03u);
}

static inline uint8_t device_type_priority(device_type_t type)
{
    switch (type) {
        case DEVICE_TYPE_KEYBOARD:
            return 3;
        case DEVICE_TYPE_XINPUT:
            return 2;
        case DEVICE_TYPE_JOYSTICK:
            return 1;
        default:
            return 0;
    }
}

static inline const char* device_type_to_str(device_type_t type)
{
    switch (type) {
        case DEVICE_TYPE_JOYSTICK:
            return "Joystick";
        case DEVICE_TYPE_XINPUT:
            return "XInput";
        case DEVICE_TYPE_KEYBOARD:
            return "Keyboard";
        default:
            return "Unknown";
    }
}

static inline device_type_t classify_hid_interface(uint8_t protocol, uint16_t vid)
{
    if (protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        return DEVICE_TYPE_KEYBOARD;
    }

    // Microsoft VID is a strong hint for Xbox/XInput-style devices.
    if (vid == 0x045E) {
        return DEVICE_TYPE_XINPUT;
    }

    // Mouse interface of composite keyboards must not override keyboard handling.
    if (protocol == HID_ITF_PROTOCOL_MOUSE) {
        return DEVICE_TYPE_UNKNOWN;
    }

    // Most gamepads/joysticks are HID protocol NONE.
    return DEVICE_TYPE_JOYSTICK;
}

static void reset_input_state(void)
{
    g_dev.button_byte1 = 0xFF;
    g_dev.button_byte2 = 0xFF;
    g_dev.lx = 0x80;
    g_dev.ly = 0x80;
    g_dev.rx = 0x80;
    g_dev.ry = 0x80;
    g_dev.has_analog = false;
}

static void clear_hid_table(void)
{
    for (uint8_t i = 0; i < CFG_TUH_HID; i++) {
        g_hid_mounted[i] = false;
        g_hid_protocol[i] = HID_ITF_PROTOCOL_NONE;
    }
}

static void reset_device_state(void)
{
    memset(&g_dev, 0, sizeof(g_dev));
    reset_input_state();
    clear_hid_table();
}

static void activate_source(uint8_t dev_addr, uint8_t instance, device_type_t type,
                            uint8_t protocol, bool xinput_custom_driver, bool reset_state)
{
    g_dev.connected = true;
    g_dev.addr = dev_addr;
    g_dev.instance = instance;
    g_dev.type = type;
    g_dev.hid_protocol = protocol;
    g_dev.xinput_custom_driver = xinput_custom_driver;

    if (reset_state) {
        reset_input_state();
    }
}

static bool select_best_hid_source(uint8_t dev_addr)
{
    uint8_t best_instance = 0xFF;
    uint8_t best_protocol = HID_ITF_PROTOCOL_NONE;
    device_type_t best_type = DEVICE_TYPE_UNKNOWN;
    uint8_t best_prio = 0;

    for (uint8_t i = 0; i < CFG_TUH_HID; i++) {
        device_type_t candidate;
        uint8_t prio;

        if (!g_hid_mounted[i]) {
            continue;
        }

        candidate = classify_hid_interface(g_hid_protocol[i], g_dev.vid);
        prio = device_type_priority(candidate);
        if (prio > best_prio) {
            best_prio = prio;
            best_type = candidate;
            best_protocol = g_hid_protocol[i];
            best_instance = i;
        }
    }

    if (best_instance == 0xFF || best_type == DEVICE_TYPE_UNKNOWN) {
        return false;
    }

    activate_source(dev_addr, best_instance, best_type, best_protocol, false, true);
    return true;
}

static void set_dpad_from_hat(uint8_t hat)
{
    bool up = false;
    bool right = false;
    bool down = false;
    bool left = false;

    switch (hat & 0x0F) {
        case 0: up = true; break;
        case 1: up = true; right = true; break;
        case 2: right = true; break;
        case 3: right = true; down = true; break;
        case 4: down = true; break;
        case 5: down = true; left = true; break;
        case 6: left = true; break;
        case 7: left = true; up = true; break;
        default: break;
    }

    psx_set_pressed(&g_dev.button_byte1, 4, up);
    psx_set_pressed(&g_dev.button_byte1, 5, right);
    psx_set_pressed(&g_dev.button_byte1, 6, down);
    psx_set_pressed(&g_dev.button_byte1, 7, left);
}

static void set_dpad_from_axes_u16(uint16_t x, uint16_t y)
{
    if (x <= 341u) {
        psx_set_pressed(&g_dev.button_byte1, 7, true);
    } else if (x >= 682u) {
        psx_set_pressed(&g_dev.button_byte1, 5, true);
    }

    if (y <= 341u) {
        psx_set_pressed(&g_dev.button_byte1, 4, true);
    } else if (y >= 682u) {
        psx_set_pressed(&g_dev.button_byte1, 6, true);
    }
}

static bool key_in_report(hid_keyboard_report_t const* report, uint8_t key)
{
    for (uint8_t i = 0; i < 6; i++) {
        if (report->keycode[i] == key) {
            return true;
        }
    }
    return false;
}

static void parse_keyboard_report(hid_keyboard_report_t const* report)
{
    reset_input_state();

    // Direction keys
    psx_set_pressed(&g_dev.button_byte1, 4, key_in_report(report, HID_KEY_ARROW_UP));
    psx_set_pressed(&g_dev.button_byte1, 5, key_in_report(report, HID_KEY_ARROW_RIGHT));
    psx_set_pressed(&g_dev.button_byte1, 6, key_in_report(report, HID_KEY_ARROW_DOWN));
    psx_set_pressed(&g_dev.button_byte1, 7, key_in_report(report, HID_KEY_ARROW_LEFT));

    // System buttons
    psx_set_pressed(&g_dev.button_byte1, 3, key_in_report(report, HID_KEY_ENTER));
    psx_set_pressed(&g_dev.button_byte1, 0,
                    key_in_report(report, HID_KEY_TAB) ||
                    key_in_report(report, HID_KEY_ESCAPE) ||
                    key_in_report(report, HID_KEY_BACKSPACE));

    // Face buttons (primary + fallback cluster)
    psx_set_pressed(&g_dev.button_byte2, 6,
                    key_in_report(report, HID_KEY_Z) || key_in_report(report, HID_KEY_SPACE)); // CROSS
    psx_set_pressed(&g_dev.button_byte2, 5, key_in_report(report, HID_KEY_X)); // CIRCLE
    psx_set_pressed(&g_dev.button_byte2, 7, key_in_report(report, HID_KEY_A)); // SQUARE
    psx_set_pressed(&g_dev.button_byte2, 4,
                    key_in_report(report, HID_KEY_S) || key_in_report(report, HID_KEY_W)); // TRIANGLE

    // Shoulder/trigger buttons
    psx_set_pressed(&g_dev.button_byte2, 2, key_in_report(report, HID_KEY_Q)); // L1
    psx_set_pressed(&g_dev.button_byte2, 3, key_in_report(report, HID_KEY_E)); // R1
    psx_set_pressed(&g_dev.button_byte2, 0,
                    key_in_report(report, HID_KEY_1) || (report->modifier & KEYBOARD_MODIFIER_LEFTSHIFT)); // L2
    psx_set_pressed(&g_dev.button_byte2, 1,
                    key_in_report(report, HID_KEY_2) || key_in_report(report, HID_KEY_3) ||
                    (report->modifier & KEYBOARD_MODIFIER_RIGHTSHIFT)); // R2
}

static bool parse_xinput_report(uint8_t const* report, uint16_t len)
{
    uint16_t buttons;
    uint8_t lt;
    uint8_t rt;
    int16_t lx;
    int16_t ly;
    int16_t rx;
    int16_t ry;
    uint16_t offset = 0;

    if (len >= 14 && len >= 2 && (report[1] == 0x14 || report[1] == 0x1E)) {
        offset = 2;
    }

    if (len < (uint16_t) (offset + 12)) {
        return false;
    }

    buttons = read_le16(&report[offset + 0]);
    lt = report[offset + 2];
    rt = report[offset + 3];
    lx = read_i16(&report[offset + 4]);
    ly = read_i16(&report[offset + 6]);
    rx = read_i16(&report[offset + 8]);
    ry = read_i16(&report[offset + 10]);

    reset_input_state();

    psx_set_pressed(&g_dev.button_byte1, 4, (buttons & XINPUT_DPAD_UP) != 0);
    psx_set_pressed(&g_dev.button_byte1, 5, (buttons & XINPUT_DPAD_RIGHT) != 0);
    psx_set_pressed(&g_dev.button_byte1, 6, (buttons & XINPUT_DPAD_DOWN) != 0);
    psx_set_pressed(&g_dev.button_byte1, 7, (buttons & XINPUT_DPAD_LEFT) != 0);
    psx_set_pressed(&g_dev.button_byte1, 3, (buttons & XINPUT_START) != 0);
    psx_set_pressed(&g_dev.button_byte1, 0, (buttons & XINPUT_BACK) != 0);
    psx_set_pressed(&g_dev.button_byte1, 1, (buttons & XINPUT_L3) != 0);
    psx_set_pressed(&g_dev.button_byte1, 2, (buttons & XINPUT_R3) != 0);

    psx_set_pressed(&g_dev.button_byte2, 2, (buttons & XINPUT_LB) != 0);
    psx_set_pressed(&g_dev.button_byte2, 3, (buttons & XINPUT_RB) != 0);
    psx_set_pressed(&g_dev.button_byte2, 0, lt > XINPUT_TRIGGER_THRESHOLD);
    psx_set_pressed(&g_dev.button_byte2, 1, rt > XINPUT_TRIGGER_THRESHOLD);
    psx_set_pressed(&g_dev.button_byte2, 6, (buttons & XINPUT_A) != 0);  // CROSS
    psx_set_pressed(&g_dev.button_byte2, 5, (buttons & XINPUT_B) != 0);  // CIRCLE
    psx_set_pressed(&g_dev.button_byte2, 7, (buttons & XINPUT_X) != 0);  // SQUARE
    psx_set_pressed(&g_dev.button_byte2, 4, (buttons & XINPUT_Y) != 0);  // TRIANGLE

    g_dev.lx = scale_signed16_to_u8(lx);
    g_dev.ly = scale_signed16_to_u8((int16_t) -ly);
    g_dev.rx = scale_signed16_to_u8(rx);
    g_dev.ry = scale_signed16_to_u8((int16_t) -ry);
    g_dev.has_analog = true;

    return true;
}

static bool parse_arduino_joystick_report(uint8_t const* report, uint16_t len)
{
    uint32_t buttons = 0;
    uint8_t hat0 = 0x08;
    uint8_t hat1 = 0x08;
    uint16_t x = 512;
    uint16_t y = 512;
    uint16_t rx = 512;
    uint16_t ry = 512;

    // ArduinoJoystickLibrary default report layout (with Report ID):
    // [0]=ReportID(0x03), [1..4]=buttons(32), [5]=hat0/hat1 packed,
    // [6..]=16-bit axis values (x,y,z,rx,ry,...)
    if (!(len >= 2 && report[0] == 0x03)) {
        return false;
    }

    // Need at least ID + buttons + hats + x/y + rx to decode safely.
    if (len < 16u) {
        return false;
    }

    buttons = read_le32(&report[1]);
    hat0 = (uint8_t) (report[5] & 0x0Fu);
    hat1 = (uint8_t) ((report[5] >> 4) & 0x0Fu);
    x = read_le16(&report[6]);
    y = read_le16(&report[8]);
    rx = read_le16(&report[12]);
    if (len >= 18u) {
        ry = read_le16(&report[14]);
    }

    reset_input_state();

    psx_set_pressed(&g_dev.button_byte2, 6, (buttons & (1u << 0)) != 0);   // CROSS
    psx_set_pressed(&g_dev.button_byte2, 5, (buttons & (1u << 1)) != 0);   // CIRCLE
    psx_set_pressed(&g_dev.button_byte2, 7, (buttons & (1u << 2)) != 0);   // SQUARE
    psx_set_pressed(&g_dev.button_byte2, 4, (buttons & (1u << 3)) != 0);   // TRIANGLE
    psx_set_pressed(&g_dev.button_byte2, 2, (buttons & (1u << 4)) != 0);   // L1
    psx_set_pressed(&g_dev.button_byte2, 3, (buttons & (1u << 5)) != 0);   // R1
    psx_set_pressed(&g_dev.button_byte2, 0, (buttons & (1u << 6)) != 0);   // L2
    psx_set_pressed(&g_dev.button_byte2, 1, (buttons & (1u << 7)) != 0);   // R2
    psx_set_pressed(&g_dev.button_byte1, 0, (buttons & (1u << 8)) != 0);   // SELECT
    psx_set_pressed(&g_dev.button_byte1, 3, (buttons & (1u << 9)) != 0);   // START
    psx_set_pressed(&g_dev.button_byte1, 1, (buttons & (1u << 10)) != 0);  // L3
    psx_set_pressed(&g_dev.button_byte1, 2, (buttons & (1u << 11)) != 0);  // R3

    if (hat0 <= 7u) {
        set_dpad_from_hat(hat0);
    } else if (hat1 <= 7u) {
        set_dpad_from_hat(hat1);
    } else {
        set_dpad_from_axes_u16(x, y);
    }

    g_dev.lx = scale_u16_to_u8(x);
    g_dev.ly = scale_u16_to_u8(y);
    g_dev.rx = scale_u16_to_u8(rx);
    g_dev.ry = scale_u16_to_u8(ry);
    g_dev.has_analog = true;

    return true;
}

static bool parse_simple_gamepad_axis_first(uint8_t const* report, uint16_t len)
{
    uint16_t offset = 0;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint8_t hat;
    uint16_t buttons;

    if (len >= 9 && report[0] <= 0x0Fu) {
        offset = 1;
    }

    if (len < (uint16_t) (offset + 7)) {
        return false;
    }

    lx = report[offset + 0];
    ly = report[offset + 1];
    rx = report[offset + 2];
    ry = report[offset + 3];
    hat = report[offset + 4] & 0x0F;
    buttons = read_le16(&report[offset + 5]);

    reset_input_state();

    psx_set_pressed(&g_dev.button_byte2, 6, (buttons & (1u << 0)) != 0);   // CROSS
    psx_set_pressed(&g_dev.button_byte2, 5, (buttons & (1u << 1)) != 0);   // CIRCLE
    psx_set_pressed(&g_dev.button_byte2, 7, (buttons & (1u << 2)) != 0);   // SQUARE
    psx_set_pressed(&g_dev.button_byte2, 4, (buttons & (1u << 3)) != 0);   // TRIANGLE
    psx_set_pressed(&g_dev.button_byte2, 2, (buttons & (1u << 4)) != 0);   // L1
    psx_set_pressed(&g_dev.button_byte2, 3, (buttons & (1u << 5)) != 0);   // R1
    psx_set_pressed(&g_dev.button_byte2, 0, (buttons & (1u << 6)) != 0);   // L2
    psx_set_pressed(&g_dev.button_byte2, 1, (buttons & (1u << 7)) != 0);   // R2
    psx_set_pressed(&g_dev.button_byte1, 0, (buttons & (1u << 8)) != 0);   // SELECT
    psx_set_pressed(&g_dev.button_byte1, 3, (buttons & (1u << 9)) != 0);   // START
    psx_set_pressed(&g_dev.button_byte1, 1, (buttons & (1u << 10)) != 0);  // L3
    psx_set_pressed(&g_dev.button_byte1, 2, (buttons & (1u << 11)) != 0);  // R3

    if (hat <= 7u) {
        set_dpad_from_hat(hat);
    }

    g_dev.lx = lx;
    g_dev.ly = ly;
    g_dev.rx = rx;
    g_dev.ry = ry;
    g_dev.has_analog = true;

    return true;
}

static bool parse_simple_gamepad_axis_buttons_hat(uint8_t const* report, uint16_t len)
{
    uint16_t offset = 0;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint16_t buttons;
    uint8_t hat;

    if (len >= 9 && report[0] <= 0x0Fu) {
        offset = 1;
    }

    if (len < (uint16_t) (offset + 7)) {
        return false;
    }

    lx = report[offset + 0];
    ly = report[offset + 1];
    rx = report[offset + 2];
    ry = report[offset + 3];
    buttons = read_le16(&report[offset + 4]);
    hat = report[offset + 6] & 0x0F;

    reset_input_state();

    psx_set_pressed(&g_dev.button_byte2, 6, (buttons & (1u << 0)) != 0);   // CROSS
    psx_set_pressed(&g_dev.button_byte2, 5, (buttons & (1u << 1)) != 0);   // CIRCLE
    psx_set_pressed(&g_dev.button_byte2, 7, (buttons & (1u << 2)) != 0);   // SQUARE
    psx_set_pressed(&g_dev.button_byte2, 4, (buttons & (1u << 3)) != 0);   // TRIANGLE
    psx_set_pressed(&g_dev.button_byte2, 2, (buttons & (1u << 4)) != 0);   // L1
    psx_set_pressed(&g_dev.button_byte2, 3, (buttons & (1u << 5)) != 0);   // R1
    psx_set_pressed(&g_dev.button_byte2, 0, (buttons & (1u << 6)) != 0);   // L2
    psx_set_pressed(&g_dev.button_byte2, 1, (buttons & (1u << 7)) != 0);   // R2
    psx_set_pressed(&g_dev.button_byte1, 0, (buttons & (1u << 8)) != 0);   // SELECT
    psx_set_pressed(&g_dev.button_byte1, 3, (buttons & (1u << 9)) != 0);   // START
    psx_set_pressed(&g_dev.button_byte1, 1, (buttons & (1u << 10)) != 0);  // L3
    psx_set_pressed(&g_dev.button_byte1, 2, (buttons & (1u << 11)) != 0);  // R3

    if (hat <= 7u) {
        set_dpad_from_hat(hat);
    }

    g_dev.lx = lx;
    g_dev.ly = ly;
    g_dev.rx = rx;
    g_dev.ry = ry;
    g_dev.has_analog = true;

    return true;
}

static bool parse_simple_gamepad_report(uint8_t const* report, uint16_t len)
{
    uint16_t offset = 0;
    uint16_t buttons;
    uint8_t hat;
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;

    if (len >= 8 && report[0] <= 0x0F) {
        offset = 1;
    }

    if (len < (uint16_t) (offset + 6)) {
        return false;
    }

    buttons = read_le16(&report[offset + 0]);
    hat = report[offset + 2] & 0x0F;
    lx = report[offset + 3];
    ly = report[offset + 4];
    rx = (len >= (uint16_t) (offset + 6)) ? report[offset + 5] : 0x80;
    ry = (len >= (uint16_t) (offset + 7)) ? report[offset + 6] : 0x80;

    reset_input_state();

    psx_set_pressed(&g_dev.button_byte2, 6, (buttons & (1u << 0)) != 0);   // CROSS
    psx_set_pressed(&g_dev.button_byte2, 5, (buttons & (1u << 1)) != 0);   // CIRCLE
    psx_set_pressed(&g_dev.button_byte2, 7, (buttons & (1u << 2)) != 0);   // SQUARE
    psx_set_pressed(&g_dev.button_byte2, 4, (buttons & (1u << 3)) != 0);   // TRIANGLE
    psx_set_pressed(&g_dev.button_byte2, 2, (buttons & (1u << 4)) != 0);   // L1
    psx_set_pressed(&g_dev.button_byte2, 3, (buttons & (1u << 5)) != 0);   // R1
    psx_set_pressed(&g_dev.button_byte2, 0, (buttons & (1u << 6)) != 0);   // L2
    psx_set_pressed(&g_dev.button_byte2, 1, (buttons & (1u << 7)) != 0);   // R2
    psx_set_pressed(&g_dev.button_byte1, 0, (buttons & (1u << 8)) != 0);   // SELECT
    psx_set_pressed(&g_dev.button_byte1, 3, (buttons & (1u << 9)) != 0);   // START
    psx_set_pressed(&g_dev.button_byte1, 1, (buttons & (1u << 10)) != 0);  // L3
    psx_set_pressed(&g_dev.button_byte1, 2, (buttons & (1u << 11)) != 0);  // R3

    if (hat <= 7u) {
        set_dpad_from_hat(hat);
    }

    g_dev.lx = lx;
    g_dev.ly = ly;
    g_dev.rx = rx;
    g_dev.ry = ry;
    g_dev.has_analog = true;

    return true;
}

void usb_host_input_init(void)
{
    reset_device_state();

    if (!tuh_inited()) {
        tusb_rhport_init_t host_init = {
            .role = TUSB_ROLE_HOST,
            .speed = TUSB_SPEED_FULL,
        };

        if (!tusb_init(BOARD_TUH_RHPORT, &host_init)) {
            printf("[USB] host init failed\n");
        }
    }

    printf("[USB] host ready (OTG host mode)\n");
}

void usb_host_input_task(void)
{
    if (!tuh_inited()) {
        return;
    }

    // Non-blocking polling keeps PSX timing loop responsive even when no USB events.
    tuh_task_ext(0, false);
    xinput_receive_report();
}

void usb_host_get_button_state(uint8_t* byte1, uint8_t* byte2)
{
    if (byte1) {
        *byte1 = g_dev.button_byte1;
    }
    if (byte2) {
        *byte2 = g_dev.button_byte2;
    }
}

bool usb_host_get_analog_state(uint8_t* lx, uint8_t* ly, uint8_t* rx, uint8_t* ry)
{
    if (!g_dev.connected || !g_dev.has_analog) {
        return false;
    }

    if (lx) {
        *lx = g_dev.lx;
    }
    if (ly) {
        *ly = g_dev.ly;
    }
    if (rx) {
        *rx = g_dev.rx;
    }
    if (ry) {
        *ry = g_dev.ry;
    }

    return true;
}

bool usb_host_is_device_connected(void)
{
    return g_dev.connected;
}

const char* usb_host_get_device_type(void)
{
    return device_type_to_str(g_dev.type);
}

// TinyUSB callbacks

void tuh_mount_cb(uint8_t daddr)
{
    (void) daddr;
}

void tuh_unmount_cb(uint8_t daddr)
{
    if (g_dev.connected && g_dev.addr == daddr) {
        reset_device_state();
        printf("[USB] device detached\n");
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
    uint16_t vid;
    uint16_t pid;
    uint8_t protocol;
    device_type_t candidate_type;

    tuh_vid_pid_get(dev_addr, &vid, &pid);
    g_dev.vid = vid;
    g_dev.pid = pid;

    // Mounted via custom XInput class driver.
    if (desc_report == NULL && desc_len == 0) {
        activate_source(dev_addr, instance, DEVICE_TYPE_XINPUT, HID_ITF_PROTOCOL_NONE, true, true);
        printf("[USB] XInput mounted: inst=%u VID=%04X PID=%04X\n", instance, g_dev.vid, g_dev.pid);
        return;
    }

    if (!g_dev.connected || g_dev.addr != dev_addr) {
        reset_device_state();
        g_dev.connected = true;
        g_dev.addr = dev_addr;
        g_dev.vid = vid;
        g_dev.pid = pid;
    }

    protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (instance < CFG_TUH_HID) {
        g_hid_mounted[instance] = true;
        g_hid_protocol[instance] = protocol;
    }

    candidate_type = classify_hid_interface(protocol, g_dev.vid);

    if (candidate_type != DEVICE_TYPE_UNKNOWN) {
        if (!g_dev.connected || g_dev.addr != dev_addr ||
            device_type_priority(candidate_type) > device_type_priority(g_dev.type) ||
            g_dev.instance == instance) {
            bool changed = (g_dev.addr != dev_addr) || (g_dev.instance != instance) || (g_dev.type != candidate_type);
            activate_source(dev_addr, instance, candidate_type, protocol, false, changed);
        }
    }

    printf("[USB] HID mounted: inst=%u proto=%u candidate=%s active=%s VID=%04X PID=%04X\n",
           instance,
           protocol,
           device_type_to_str(candidate_type),
           device_type_to_str(g_dev.type),
           g_dev.vid,
           g_dev.pid);

    if (!tuh_hid_receive_report(dev_addr, instance)) {
        printf("[USB] failed to arm HID report reception (inst=%u)\n", instance);
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    if (!g_dev.connected || g_dev.addr != dev_addr) {
        return;
    }

    if (instance < CFG_TUH_HID) {
        g_hid_mounted[instance] = false;
        g_hid_protocol[instance] = HID_ITF_PROTOCOL_NONE;
    }

    if (g_dev.xinput_custom_driver && g_dev.instance == instance) {
        reset_device_state();
        return;
    }

    if (g_dev.instance == instance) {
        if (!select_best_hid_source(dev_addr)) {
            reset_device_state();
        }
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    bool parsed = false;
    bool source_is_active;
    device_type_t source_type;
    uint8_t source_protocol = HID_ITF_PROTOCOL_NONE;

    if (!g_dev.connected || g_dev.addr != dev_addr) {
        return;
    }

    source_is_active = (instance == g_dev.instance);

    if (g_dev.xinput_custom_driver) {
        source_type = DEVICE_TYPE_XINPUT;
    } else {
        if (instance < CFG_TUH_HID && g_hid_mounted[instance]) {
            source_protocol = g_hid_protocol[instance];
        } else {
            source_protocol = tuh_hid_interface_protocol(dev_addr, instance);
        }

        source_type = classify_hid_interface(source_protocol, g_dev.vid);

        if (source_type == DEVICE_TYPE_UNKNOWN) {
            goto queue_next;
        }

        // Only parse inactive interfaces when they have higher priority (e.g., keyboard over joystick).
        if (!source_is_active && device_type_priority(source_type) <= device_type_priority(g_dev.type)) {
            goto queue_next;
        }

        if (!source_is_active) {
            activate_source(dev_addr, instance, source_type, source_protocol, false, true);
            source_is_active = true;
        }
    }

    switch (source_type) {
        case DEVICE_TYPE_KEYBOARD:
            if (len >= sizeof(hid_keyboard_report_t)) {
                parse_keyboard_report((hid_keyboard_report_t const*) report);
                parsed = true;
            }
            break;

        case DEVICE_TYPE_XINPUT:
            parsed = parse_xinput_report(report, len);
            if (!parsed) {
                parsed = parse_simple_gamepad_report(report, len);
            }
            break;

        case DEVICE_TYPE_JOYSTICK:
            if (source_is_active) {
                // Keep explicit VID/PID branching, but avoid auto-detect heuristics.
                // Leonardo (ArduinoJoystickLibrary) and compatible boards.
                if (is_arduino_vid(g_dev.vid)) {
                    parsed = parse_arduino_joystick_report(report, len);
                    if (!parsed) {
                        parsed = parse_simple_gamepad_axis_first(report, len);
                    }
                    if (!parsed) {
                        parsed = parse_simple_gamepad_report(report, len);
                    }
                }
                // Specific non-Leonardo profile observed in testing.
                else if (g_dev.vid == 0x0925u && g_dev.pid == 0x8888u) {
                    parsed = parse_simple_gamepad_axis_buttons_hat(report, len);
                    if (!parsed) {
                        parsed = parse_simple_gamepad_axis_first(report, len);
                    }
                    if (!parsed) {
                        parsed = parse_simple_gamepad_report(report, len);
                    }
                }
                // Generic HID joystick fallback order.
                else {
                    parsed = parse_simple_gamepad_axis_first(report, len);
                    if (!parsed) {
                        parsed = parse_simple_gamepad_report(report, len);
                    }
                    if (!parsed) {
                        parsed = parse_xinput_report(report, len);
                    }
                    if (!parsed) {
                        // Allow ArduinoJoystickLibrary-compatible clones with non-Arduino VID.
                        parsed = parse_arduino_joystick_report(report, len);
                    }
                }
            }
            break;

        default:
            break;
    }

    if (!parsed && source_type == DEVICE_TYPE_XINPUT && len >= 14) {
        (void) parse_xinput_report(report, len);
    }

queue_next:
    if (!g_dev.xinput_custom_driver) {
        if (!tuh_hid_receive_report(dev_addr, instance)) {
            printf("[USB] failed to queue next HID report (inst=%u)\n", instance);
        }
    }
}
