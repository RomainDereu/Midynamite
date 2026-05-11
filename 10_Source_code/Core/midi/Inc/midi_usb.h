/*
 * midi_usb.h
 *
 *  Created on: Jul 18, 2025
 *      Author: Romain Dereu
 */

#ifndef INC_MIDI_USB_H_
#define INC_MIDI_USB_H_

#include <stdint.h>

enum {
	MIDI_USB_OFF = 0,
	MIDI_USB_OUT,
	MIDI_USB_THRU,
	MIDI_USB_THRU_OUT
};

static inline uint8_t midi_usb_mode_allows_out(uint8_t usb_mode)
{
    return (uint8_t)((usb_mode == MIDI_USB_OUT) || (usb_mode == MIDI_USB_THRU_OUT));
}

static inline uint8_t midi_usb_mode_allows_thru(uint8_t usb_mode)
{
    return (uint8_t)((usb_mode == MIDI_USB_THRU) || (usb_mode == MIDI_USB_THRU_OUT));
}

void send_usb_midi_message(uint8_t *midi_message, uint8_t length);



#endif /* INC_MIDI_USB_H_ */
