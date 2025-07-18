/*
 * midi_usb.c
 *
 *  Created on: Jul 18, 2025
 *      Author: Romain Dereu
 */


#include "usbd_midi.h"

extern USBD_HandleTypeDef hUsbDeviceFS;

void send_usb_midi_message(uint8_t *midi_message, uint8_t length) {
    if (USBD_MIDI_GetState(&hUsbDeviceFS) != MIDI_IDLE) return;

    uint8_t cin;
    uint8_t status = midi_message[0];

    switch (status & 0xF0) {
        case 0x80: case 0x90: case 0xA0:
        case 0xB0: case 0xE0:
            cin = 0x08; break;  // 3-byte
        case 0xC0: case 0xD0:
            cin = 0x0C; break;  // 2-byte
        case 0xF0:
            cin = 0x05; break;  // SysEx
        default:
            cin = 0x0F; break;  // Single-byte (e.g., real-time)
    }

    uint8_t packetsBuffer[4] = {
        (0x00 << 4) | cin,  // Cable 0
        midi_message[0],
        (length > 1) ? midi_message[1] : 0,
        (length > 2) ? midi_message[2] : 0
    };

    USBD_MIDI_SendPackets(&hUsbDeviceFS, packetsBuffer, 4);
}
