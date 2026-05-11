/*
 * midi_arp.h
 *
 *  Created on: Feb 21, 2026
 *      Author: Romain Dereu
 */


#ifndef MIDI_INC_MIDI_ARP_H_
#define MIDI_INC_MIDI_ARP_H_

#include <stdint.h>
#include "midi_transform.h"

#define ARP_MAX_NOTES 128

void arp_handle_midi_note(const midi_note *msg);
uint8_t arp_handle_midi_cc64(const midi_note *msg);

void arp_on_tempo_tick(void);
void arp_panic_clear(void);

#ifdef UNIT_TEST
void arp_state_reset(void);
void arp_sync_hold_mode(void);
uint8_t arp_get_pressed_keys(uint8_t *out_notes);
uint8_t arp_get_pressed_key_count(void);
#endif

#endif /* MIDI_INC_MIDI_ARP_H_ */
