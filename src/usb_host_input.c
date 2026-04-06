/*
 * PSX Controller Bit-Banging Simulator for Raspberry Pi Pico
 * USB Host Input Handler Implementation
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

#include <stdio.h>
#include <string.h>
#include "usb_host_input.h"
#include "pico/stdlib.h"

// USB Host includes
#include "tusb.h"

// ============================================================================
// USB Host Input State
// ============================================================================

#define MAX_DEVICES 1  // Support single USB device for now

typedef enum {
    DEVICE_TYPE_UNKNOWN,
    DEVICE_TYPE_JOYSTICK,
    DEVICE_TYPE_XINPUT,
    DEVICE_TYPE_KEYBOARD,
} device_type_t;

typedef struct {
    bool connected;
    device_type_t type;
    uint8_t addr;
    uint8_t instance;
    
    // Button state (PSX format)
    uint8_t button_byte1;  // SELECT, L3, R3, START, UP, RIGHT, DOWN, LEFT
    uint8_t button_byte2;  // L2, R2, L1, R1, TRIANGLE, CIRCLE, CROSS, SQUARE
    
    // Analog sticks
    uint8_t lx, ly;
    uint8_t rx, ry;
} usb_device_t;

static usb_device_t connected_device = {0};

// ============================================================================
// USB Host Initialization
// ============================================================================

void usb_host_input_init(void)
{
    printf("Initializing USB Host for input devices...\n");
    
    // Initialize TinyUSB in host mode
    // tusb_init() is called during board initialization
    // Just ensure we're ready to handle devices
    
    memset(&connected_device, 0, sizeof(connected_device));
    connected_device.button_byte1 = 0xFF;  // All buttons released
    connected_device.button_byte2 = 0xFF;
    
    printf("USB Host initialized - waiting for device connection\n");
}

void usb_host_input_task(void)
{
    // USB Host task for event handling
    // This must be called regularly from the main loop
    // TODO: Implement actual TinyUSB task call
    // For now, this is a placeholder
}

// ============================================================================
// USB Device Connection Callbacks (called by TinyUSB)
// ============================================================================

// Invoked when a device is mounted
void tuh_mount_cb(uint8_t daddr)
{
    printf("USB Device mounted at address %u\n", daddr);
    
    if (connected_device.connected) {
        printf("WARNING: Already have a device connected, ignoring new device\n");
        return;
    }
    
    connected_device.addr = daddr;
    connected_device.connected = true;
    connected_device.type = DEVICE_TYPE_UNKNOWN;
}

// Invoked when a device is unmounted
void tuh_unmount_cb(uint8_t daddr)
{
    printf("USB Device unmounted from address %u\n", daddr);
    
    if (connected_device.addr == daddr) {
        memset(&connected_device, 0, sizeof(connected_device));
        connected_device.button_byte1 = 0xFF;
        connected_device.button_byte2 = 0xFF;
    }
}

// ============================================================================
// HID Host Callbacks
// ============================================================================

// Invoked when HID Report is received
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    if (dev_addr != connected_device.addr) {
        return;
    }
    
    // TODO: Parse HID report based on device type
    // Basic HID report parsing for Joystick and Keyboard
    
    // For now, just prevent unused warnings
    (void)instance;
    (void)report;
    (void)len;
    
    // TODO: Request next report with proper TinyUSB function
    // if (!tuh_hid_receive_report(dev_addr, instance)) {
    //     printf("Failed to request next HID report\n");
    // }
}

// Invoked when HID Report descriptor is received
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
    printf("HID interface mounted: dev_addr=%u, instance=%u, desc_len=%u\n", dev_addr, instance, desc_len);
    
    if (dev_addr != connected_device.addr) {
        return;
    }
    
    connected_device.instance = instance;
    
    // Analyze HID report descriptor to determine device type
    // Basic detection based on descriptor content
    // (Will be improved with proper HID parser)
    
    // TODO: Implement HID descriptor parser
    // For now, assume it's a Joystick if we get a valid descriptor
    connected_device.type = DEVICE_TYPE_JOYSTICK;
    printf("Device type detected: %s\n", usb_host_get_device_type());
    
    // TODO: Request first HID report with proper TinyUSB function
    // if (!tuh_hid_receive_report(dev_addr, instance)) {
    //     printf("Failed to request initial HID report\n");
    // }
}

// Invoked when HID interface is unmounted
void tuh_hid_unmount_cb(uint8_t dev_addr, uint8_t instance)
{
    printf("HID interface unmounted: dev_addr=%u, instance=%u\n", dev_addr, instance);
    
    if (dev_addr == connected_device.addr && instance == connected_device.instance) {
        connected_device.type = DEVICE_TYPE_UNKNOWN;
        connected_device.button_byte1 = 0xFF;
        connected_device.button_byte2 = 0xFF;
    }
}

// ============================================================================
// Public API
// ============================================================================

void usb_host_get_button_state(uint8_t *byte1, uint8_t *byte2)
{
    if (byte1) {
        *byte1 = connected_device.button_byte1;
    }
    if (byte2) {
        *byte2 = connected_device.button_byte2;
    }
}

bool usb_host_get_analog_state(uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry)
{
    if (!connected_device.connected || (connected_device.type != DEVICE_TYPE_JOYSTICK && 
                                         connected_device.type != DEVICE_TYPE_XINPUT)) {
        return false;
    }
    
    if (lx) *lx = connected_device.lx;
    if (ly) *ly = connected_device.ly;
    if (rx) *rx = connected_device.rx;
    if (ry) *ry = connected_device.ry;
    
    return true;
}

bool usb_host_is_device_connected(void)
{
    return connected_device.connected;
}

const char* usb_host_get_device_type(void)
{
    switch (connected_device.type) {
        case DEVICE_TYPE_JOYSTICK: return "Joystick";
        case DEVICE_TYPE_XINPUT:   return "XInput";
        case DEVICE_TYPE_KEYBOARD: return "Keyboard";
        default:                   return "Unknown";
    }
}
