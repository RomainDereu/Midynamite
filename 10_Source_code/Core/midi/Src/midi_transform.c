/*
 * midi_transform.c
 *
 *  Created on: Sep 5, 2025
 *      Author: Romain Dereu
 */


#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "menus.h"

#include "midi_transform.h"
#include "midi_arp.h"
#include "midi_dispatch.h"


#include "main.h"
#include "midi_usb.h"
#include "memory_main.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

typedef enum {
    SPLIT_TARGET_BOTH = 0,
    SPLIT_TARGET_LOW,
    SPLIT_TARGET_HIGH
} split_output_target_t;

typedef struct {
    uint8_t uart1;
    uint8_t uart2;
    uint8_t usb;
} output_route_t;


static uint8_t g_pipeline_split_route = 3;
static split_output_target_t g_pipeline_split_target = SPLIT_TARGET_BOTH;

static uint8_t g_split_velocity_note_high_count[16][128];
static uint8_t g_split_velocity_note_low_count[16][128];

static output_route_t midi_route_from_settings(void);
static void emit_midi_to_route(const midi_note *msg, const output_route_t *route);

// Circular buffer instance declared externally
extern midi_modify_circular_buffer midi_modify_buff;


// ---------------------
// Helpers
// ---------------------
uint8_t midi_message_length(uint8_t status)
{
    // Realtime (F8–FF) are 1 byte
    if (status >= 0xF8) return 1;

    switch (status) {
        case 0xF1: return 2; // MTC Quarter Frame
        case 0xF2: return 3; // Song Position
        case 0xF3: return 2; // Song Select
        case 0xF6: return 1; // Tune Request
        case 0xF7: return 1; // EOX
        default: break;
    }

    // Channel voice:
    const uint8_t hi = (uint8_t)(status & 0xF0);
    return (hi == 0xC0 || hi == 0xD0) ? 2 : 3;
}


uint8_t midi_is_note_message(const midi_note *msg, uint8_t *is_note_on)
{
    if ((msg == NULL) || (is_note_on == NULL)) {
        return 0;
    }

    const uint8_t status_nibble = (uint8_t)(msg->status & 0xF0);

    if (status_nibble == 0x90) {
        *is_note_on = (msg->velocity > 0) ? 1 : 0;
        return 1;
    }

    if (status_nibble == 0x80) {
        *is_note_on = 0;
        return 1;
    }

    return 0;
}



void midi_change_channel(midi_note *midi_msg, uint8_t send_to_midi_channel) {
    uint8_t status = midi_msg->status;

    if (status >= 0x80 && status <= 0xEF) {
        uint8_t status_nibble = status & 0xF0;
        midi_msg->status = status_nibble | ((send_to_midi_channel - 1) & 0x0F);
    }
}




static void change_velocity(midi_note *midi_msg)
{
    const uint8_t status_nibble = (uint8_t)(midi_msg->status & 0xF0);
    uint8_t is_note_on = 0;
    const uint8_t is_note = midi_is_note_message(midi_msg, &is_note_on);

    const uint8_t is_cc = (status_nibble == 0xB0) ? 1 : 0;
    const uint8_t is_cc64 = (is_cc && midi_msg->note == 64) ? 1 : 0;

    // Velocity shaping applies to note messages and to CC values,
    // except sustain pedal (CC64) which must remain untouched.
    if (!is_note && !is_cc) {
        return;
    }
    if (is_cc64) {
        return;
    }

    // Keep explicit Note Off with velocity 0 unchanged,
    // while allowing non-zero Note Off release velocity to be shaped.
    if (is_note && is_note_on == 0 && midi_msg->velocity == 0) {
        return;
    }


    // int32 to avoid overflow
    int32_t velocity = (int32_t)midi_msg->velocity;

    if (save_get(MODIFY_VELOCITY_TYPE) == MIDI_MODIFY_CHANGED_VEL) {
        velocity += (int32_t)save_get(MODIFY_VEL_PLUS_MINUS);
    } else if (save_get(MODIFY_VELOCITY_TYPE) == MIDI_MODIFY_FIXED_VEL) {
        velocity = (int32_t)save_get(MODIFY_VEL_ABSOLUTE);
    }

    if (velocity < 0)   velocity = 0;
    if (velocity > 127) velocity = 127;

    midi_msg->velocity = (uint8_t)velocity;
}


// ---------------------
// Circular buffer logic
// ---------------------
void midi_buffer_push(uint8_t byte) {
    uint16_t next = (midi_modify_buff.head + 1) % MIDI_MODIFY_BUFFER_SIZE;
    if (next != midi_modify_buff.tail) { // Not full
        midi_modify_buff.data[midi_modify_buff.head] = byte;
        midi_modify_buff.head = next;
    }
}

uint8_t midi_buffer_pop(uint8_t *byte) {
    if (midi_modify_buff.head == midi_modify_buff.tail)
        return 0; // Empty
    *byte = midi_modify_buff.data[midi_modify_buff.tail];
    midi_modify_buff.tail = (midi_modify_buff.tail + 1) % MIDI_MODIFY_BUFFER_SIZE;
    return 1;
}



// ---------------------
// MIDI parse & dispatch
// ---------------------
void calculate_incoming_midi() {
    static uint8_t running_status = 0;
    static midi_note msg;
    static uint8_t byte_count = 0;
    static uint8_t expected_length = 0;

    uint8_t byte;
    while (midi_buffer_pop(&byte)) {
        // Real-time messages (do not affect parsing)
        if (byte >= 0xF8) {
            const uint8_t usb_mode = (uint8_t)save_get(SETTINGS_SEND_USB);
            if ((save_get(SETTINGS_MIDI_THRU) == 1) || (midi_usb_mode_allows_thru(usb_mode) != 0)) {
                midi_note rt = { .status = byte, .note = 0, .velocity = 0 };
                pipeline_start(&rt);
            }
            continue;
        }

        // SysEx (skip until 0xF7)
        if (byte == 0xF0) {
            running_status = 0;
            while (midi_buffer_pop(&byte) && byte != 0xF7);
            continue;
        }

        if (byte & 0x80) {
            // Status byte
            if (byte >= 0xF0) {
                // System common — unsupported
                running_status = 0;
                continue;
            }

            running_status = byte;
            msg.status = byte;

            expected_length = midi_message_length(byte);
            byte_count = 1; // waiting for data
            continue;
        }

        // Data byte
        if (running_status == 0) continue; // no valid running status

        if (byte_count == 0) {
            // We assume running status is in effect, start a new message
            msg.status = running_status;

            expected_length = midi_message_length(running_status);

            msg.note = byte;
            byte_count = 2;
            if (expected_length == 2) {
                pipeline_start(&msg);
                byte_count = 0;
            }
        } else if (byte_count == 1) {
            msg.note = byte;
            byte_count++;
            if (expected_length == 2) {
                pipeline_start(&msg);
                byte_count = 0;
            }
        } else if (byte_count == 2) {
            msg.velocity = byte;
            pipeline_start(&msg);
            byte_count = 0;
        }
    }
}

// ---------------------
// Filters / blockers
// ---------------------
static uint8_t is_channel_blocked(uint8_t status_byte) {
    if (!save_get(SETTINGS_CHANNEL_FILTER)) return 0;

    uint8_t status_nibble = status_byte & 0xF0;
    uint8_t channel = status_byte & 0x0F;

    if (status_nibble >= 0x80 && status_nibble <= 0xE0) {
        return (save_get(SETTINGS_FILTERED_CH) >> channel) & 0x01;
    }

    return 0;
}


static uint8_t split_type_is_high(const midi_note *msg)
{
    if (msg == NULL) return 0;

    const uint8_t split_type = (uint8_t)save_get(SPLIT_TYPE);
    if (split_type == 1) {
        const uint8_t status_nibble = (uint8_t)(msg->status & 0xF0);
        if (status_nibble >= 0x80 && status_nibble <= 0xE0) {
            const uint8_t channel = (uint8_t)((msg->status & 0x0F) + 1);
            return (uint8_t)(channel >= (uint8_t)save_get(SPLIT_MIDI_CHANNEL));
        }
        return 0;
    }

    if (split_type == 2) {
        uint8_t is_note_on = 0;
        if (midi_is_note_message(msg, &is_note_on) != 0) {
            const uint8_t channel = (uint8_t)(msg->status & 0x0F);
            const uint8_t note = msg->note;
            const uint8_t high_by_velocity = (uint8_t)(msg->velocity >= (uint8_t)save_get(SPLIT_VELOCITY));
            uint8_t *high_count = &g_split_velocity_note_high_count[channel][note];
            uint8_t *low_count = &g_split_velocity_note_low_count[channel][note];

            if (is_note_on != 0) {
                if (high_by_velocity != 0) {
                    if (*high_count < 255) {
                        (*high_count)++;
                    }
                } else {
                    if (*low_count < 255) {
                        (*low_count)++;
                    }
                }
                return high_by_velocity;
            }

            if (*high_count != 0) {
                (*high_count)--;
                return 1;
            }

            if (*low_count != 0) {
                (*low_count)--;
                return 0;
            }

            return high_by_velocity;

        }
        return 0;
    }

    return (uint8_t)(msg->note >= (uint8_t)save_get(SPLIT_NOTE));
}



static uint8_t split_route_allows_menu(menu_list_t menu)
{
   uint8_t active_route = g_pipeline_split_route;

	if (save_get(SPLIT_CURRENTLY_SENDING) == 1) {
		if (g_pipeline_split_target == SPLIT_TARGET_LOW) {
			active_route = (uint8_t)save_get(SPLIT_SEND_CH1);
		} else if (g_pipeline_split_target == SPLIT_TARGET_HIGH) {
			active_route = (uint8_t)save_get(SPLIT_SEND_CH2);
		}
	}

	if (active_route == 3) return 1;
	if (active_route == 0) return 0;


    uint8_t slot = 0;
    switch (menu) {
        case MENU_MODIFY:      slot = 0; break;
        case MENU_TRANSPOSE:   slot = 1; break;
        case MENU_ARPEGGIATOR: slot = 2; break;
        case MENU_DISPATCH:    slot = 3; break;
        default:               return 1;
    }

    const uint32_t mask = (uint32_t)save_get(SPLIT_MENU_MASK);
    const uint8_t menu_mask = (uint8_t)((mask >> (slot * 2)) & 0x03);
    if (menu_mask == 0) return 0;
    if (menu_mask == 3) return 1;
    return (uint8_t)(menu_mask == active_route);
}

static void split_update_route_from_channel(uint8_t channel)
{
    if (save_get(SPLIT_CURRENTLY_SENDING) == 0) {
        return;
    }

    const uint8_t low_channel = (uint8_t)save_get(SPLIT_MIDI_CH1);
    const uint8_t high_channel = (uint8_t)save_get(SPLIT_MIDI_CH2);
    const uint8_t low_route = (uint8_t)save_get(SPLIT_SEND_CH1);
    const uint8_t high_route = (uint8_t)save_get(SPLIT_SEND_CH2);

    if ((low_channel == 0) || (high_channel == 0) || (low_channel == high_channel)) {
        return;
    }

    if (channel == low_channel) {
        g_pipeline_split_target = SPLIT_TARGET_LOW;
        g_pipeline_split_route = low_route;
        return;
    }

    if (channel == high_channel) {
        g_pipeline_split_target = SPLIT_TARGET_HIGH;
        g_pipeline_split_route = high_route;
    }
}

static void split_send_direct(midi_note *msg, uint8_t length)
{
    (void)length;
    emit_midi(msg);
}

static void pipeline_execute_from(uint8_t stage_index, midi_note *midi_msg);

static uint8_t split_route_cc64_for_both_parts(const midi_note *midi_msg, uint8_t next_stage)
{
    if (midi_msg == NULL) {
        return 0;
    }

    const uint8_t status_nibble = (uint8_t)(midi_msg->status & 0xF0);
    if ((status_nibble != 0xB0) || ((midi_msg->note & 0x7F) != 64)) {
        return 0;
    }

    const uint8_t split_send_modes[2] = {
        (uint8_t)save_get(SPLIT_SEND_CH1),
        (uint8_t)save_get(SPLIT_SEND_CH2)
    };
    const uint8_t split_channels[2] = {
        (uint8_t)save_get(SPLIT_MIDI_CH1),
        (uint8_t)save_get(SPLIT_MIDI_CH2)
    };
    const split_output_target_t split_targets[2] = {
        SPLIT_TARGET_LOW,
        SPLIT_TARGET_HIGH
    };

    for (uint8_t i = 0; i < 2; ++i) {
        midi_note split_part_msg = *midi_msg;

        if (split_channels[i] > 0) {
            midi_change_channel(&split_part_msg, split_channels[i]);
        }

        g_pipeline_split_target = split_targets[i];
        g_pipeline_split_route = split_send_modes[i];

        if (split_send_modes[i] == 0) {
            split_send_direct(&split_part_msg, midi_message_length(split_part_msg.status));
        } else {
            pipeline_execute_from(next_stage, &split_part_msg);
        }
    }

    return 1;
}




typedef uint8_t (*pipeline_stage_fn_t)(midi_note *midi_msg, uint8_t length, uint8_t next_stage);

typedef enum {
    PIPELINE_STAGE_SPLIT = 0,
    PIPELINE_STAGE_MODIFY,
    PIPELINE_STAGE_TRANSPOSE,
    PIPELINE_STAGE_ARP,
    PIPELINE_STAGE_FINAL,
    PIPELINE_STAGE_COUNT
} pipeline_stage_index_t;

static uint8_t pipeline_stage_split(midi_note *midi_msg, uint8_t length, uint8_t next_stage);
static uint8_t pipeline_stage_modify(midi_note *midi_msg, uint8_t length, uint8_t next_stage);
static uint8_t pipeline_stage_transpose(midi_note *midi_msg, uint8_t length, uint8_t next_stage);
static uint8_t pipeline_stage_arp(midi_note *midi_msg, uint8_t length, uint8_t next_stage);
static uint8_t pipeline_stage_final(midi_note *midi_msg, uint8_t length, uint8_t next_stage);

static const pipeline_stage_fn_t g_pipeline_stages[PIPELINE_STAGE_COUNT] = {
    pipeline_stage_split,
    pipeline_stage_modify,
    pipeline_stage_transpose,
    pipeline_stage_arp,
    pipeline_stage_final
};

static void pipeline_execute_from(uint8_t stage_index, midi_note *midi_msg)
{
    for (uint8_t idx = stage_index; idx < PIPELINE_STAGE_COUNT; ++idx) {
        const uint8_t length = midi_message_length(midi_msg->status);
        const uint8_t next_stage = (uint8_t)(idx + 1);

        if (g_pipeline_stages[idx](midi_msg, length, next_stage) != 0) {
            return;
        }
    }
}





// ---------------------
// Pipeline entry
// ---------------------
void pipeline_start(midi_note *midi_msg)
{
    const uint8_t status = midi_msg->status;
    const uint8_t usb_mode = (uint8_t)save_get(SETTINGS_SEND_USB);
    const uint8_t midi_thru_enabled = (uint8_t)(save_get(SETTINGS_MIDI_THRU) == 1);
    const uint8_t usb_thru_enabled = midi_usb_mode_allows_thru(usb_mode);

    g_pipeline_split_route = 3;
    g_pipeline_split_target = SPLIT_TARGET_BOTH;

    if (is_channel_blocked(status)) return;

    const uint8_t any_pipeline_enabled =
        (uint8_t)(
            (save_get(SPLIT_CURRENTLY_SENDING) == 1)
            || (save_get(MODIFY_CURRENTLY_SENDING) == 1)
            || (save_get(TRANSPOSE_CURRENTLY_SENDING) == 1)
            || (save_get(ARPEGGIATOR_CURRENTLY_SENDING) == 1)
            || (save_get(DISPATCH_CURRENTLY_SENDING) == 1)
        );

    if ((midi_thru_enabled != 0) || (usb_thru_enabled != 0)) {
        output_route_t thru_route = midi_route_from_settings();
        thru_route.usb = usb_thru_enabled;
        if (midi_thru_enabled == 0) {
            thru_route.uart1 = 0;
            thru_route.uart2 = 0;
        }
        emit_midi_to_route(midi_msg, &thru_route);
    }

    if (any_pipeline_enabled != 0) {
        pipeline_execute_from(PIPELINE_STAGE_SPLIT, midi_msg);
        return;
    }
}

// ---------------------
// Split pipeline
// ---------------------
static uint8_t pipeline_stage_split(midi_note *midi_msg, uint8_t length, uint8_t next_stage)
{
    (void)length;

    if (save_get(SPLIT_CURRENTLY_SENDING) == 0) {
        return 0;
    }

    if (split_route_cc64_for_both_parts(midi_msg, next_stage) != 0) {
        return 1;
    }

    midi_note split_msg = *midi_msg;
    const uint8_t split_is_high = split_type_is_high(&split_msg);
    const uint8_t split_channel = (split_is_high != 0)
                                  ? (uint8_t)save_get(SPLIT_MIDI_CH2)
                                  : (uint8_t)save_get(SPLIT_MIDI_CH1);
    const save_field_t send_field = (split_is_high != 0) ? SPLIT_SEND_CH2 : SPLIT_SEND_CH1;
    const uint8_t split_send_mode = (uint8_t)save_get(send_field);

    if (split_channel > 0) {
        midi_change_channel(&split_msg, split_channel);
    }

    g_pipeline_split_target = (split_is_high != 0) ? SPLIT_TARGET_HIGH : SPLIT_TARGET_LOW;
    g_pipeline_split_route = split_send_mode;

    if (split_send_mode == 0) {
        split_send_direct(&split_msg, midi_message_length(split_msg.status));
        return 1;
    }

    *midi_msg = split_msg;
    return 0;
}


// ---------------------
// Modify pipeline
// ---------------------
static uint8_t pipeline_stage_modify(midi_note *midi_msg, uint8_t length, uint8_t next_stage)
{
    (void)length;

    if (save_get(MODIFY_CURRENTLY_SENDING) == 0 || split_route_allows_menu(MENU_MODIFY) == 0) {
        return 0;
    }

    change_velocity(midi_msg);

    const uint8_t send_ch1 = (uint8_t)save_get(MODIFY_SEND_TO_MIDI_CH1);
    if (send_ch1 != 0) {
        midi_note midi_note_1 = *midi_msg;
        if (send_ch1 >= 2) {
            midi_change_channel(&midi_note_1, (uint8_t)(send_ch1 - 1));
        }
        split_update_route_from_channel((uint8_t)((midi_note_1.status & 0x0F) + 1));
        pipeline_execute_from(next_stage, &midi_note_1);
    }

    const uint8_t send_ch2 = (uint8_t)save_get(MODIFY_SEND_TO_MIDI_CH2);
    if (send_ch2 != 0) {
        midi_note midi_note_2 = *midi_msg;
        if (send_ch2 >= 2) {
            midi_change_channel(&midi_note_2, (uint8_t)(send_ch2 - 1));
        }
        split_update_route_from_channel((uint8_t)((midi_note_2.status & 0x0F) + 1));
        pipeline_execute_from(next_stage, &midi_note_2);
    }

    return 1;
}

// ---------------------
// Scale helpers
// ---------------------
static void get_mode_scale(uint8_t mode, uint8_t *scale) {
    const uint8_t mode_intervals[7][7] = {
        {0, 2, 4, 5, 7, 9, 11},  // Ionian
        {0, 2, 3, 5, 7, 9, 10},  // Dorian
        {0, 1, 3, 5, 7, 8, 10},  // Phrygian
        {0, 2, 4, 6, 7, 9, 11},  // Lydian
        {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
        {0, 2, 3, 5, 7, 8, 10},  // Aeolian
        {0, 1, 3, 5, 6, 8, 10}   // Locrian
    };
    memcpy(scale, mode_intervals[mode % AMOUNT_OF_MODES], 7);
}

static uint8_t note_in_scale(uint8_t note, const uint8_t *scale, uint8_t base_note) {
    int semitone_from_root = ((note - base_note) + 120) % 12;
    for (int i = 0; i < 7; i++) {
        if (scale[i] == semitone_from_root) return 1;
    }
    return 0;
}

// Snap note up/down by semitones until in scale (max 12 semitones search)
static uint8_t snap_note_to_scale(uint8_t note, const uint8_t *scale, uint8_t base_note) {
    uint8_t up_note = note;
    uint8_t down_note = note;

    for (int i = 0; i < 12; i++) {
        if (note_in_scale(down_note, scale, base_note)) return down_note;
        if (note_in_scale(up_note,  scale, base_note)) return up_note;

        if (down_note > 0)   down_note--;
        if (up_note   < 127) up_note++;
    }
    return note;  // fallback (should not happen)
}

// ---------------------
// Transpose math
// ---------------------
static uint8_t midi_transpose_notes(uint8_t note) {
    uint8_t mode = save_get(TRANSPOSE_TRANSPOSE_SCALE) % AMOUNT_OF_MODES;

    uint8_t scale_intervals[7];
    get_mode_scale(mode, scale_intervals);

    uint8_t base = save_get(TRANSPOSE_BASE_NOTE);

    // Snap note to scale if not in scale
    if (!note_in_scale(note, scale_intervals, base)) {
        note = snap_note_to_scale(note, scale_intervals, base);
    }

    int16_t semitone_from_root = ((note - base) + 120) % 12;
    int16_t base_octave_offset = (note - base) / 12;

    int degree = -1;
    for (int i = 0; i < 7; i++) {
        if (scale_intervals[i] == semitone_from_root) {
            degree = i;
            break;
        }
    }

    // Safety fallback
    if (degree == -1) {
        return note;
    }

    static const int degree_shifts[10] = {
        -7, -5, -4, -3, -2, 2, 3, 4, 5, 7
    };
    int shift = degree_shifts[ save_get(TRANSPOSE_INTERVAL) ];

    int new_degree = degree + shift;
    int octave_shift = 0;

    while (new_degree < 0) {
        new_degree += 7;
        octave_shift -= 1;
    }
    while (new_degree >= 7) {
        new_degree -= 7;
        octave_shift += 1;
    }

    int new_note = base
                 + scale_intervals[new_degree]
                 + (base_octave_offset + octave_shift) * 12;

    if (new_note < 0)   new_note = 0;
    if (new_note > 127) new_note = 127;

    return (uint8_t)new_note;
}

static void midi_pitch_shift(midi_note *midi_msg) {
    uint8_t is_note_on = 0;

    if (midi_is_note_message(midi_msg, &is_note_on)) {
        int16_t note = midi_msg->note;

        if (save_get(TRANSPOSE_TRANSPOSE_TYPE) == MIDI_TRANSPOSE_SCALED) {
            note = midi_transpose_notes((uint8_t)note);
        }
        else if (save_get(TRANSPOSE_TRANSPOSE_TYPE) == MIDI_TRANSPOSE_SHIFT) {
            note += (int16_t)(int32_t)save_get(TRANSPOSE_MIDI_SHIFT_VALUE);
        }

        if (note < 0)   note = 0;
        if (note > 127) note = 127;

        midi_msg->note = (uint8_t)note;
    }
}
// ---------------------
// Transpose pipeline
// ---------------------
static uint8_t pipeline_stage_transpose(midi_note *midi_msg, uint8_t length, uint8_t next_stage)
{
    (void)length;

    if (save_get(TRANSPOSE_CURRENTLY_SENDING) == 0 || split_route_allows_menu(MENU_TRANSPOSE) == 0) {
        return 0;
    }

    if (save_get(TRANSPOSE_SEND_ORIGINAL) == 1) {
        midi_note pre_shift_msg = *midi_msg;

        if (save_get(TRANSPOSE_TRANSPOSE_TYPE) == MIDI_TRANSPOSE_SCALED) {
            uint8_t mode = save_get(TRANSPOSE_TRANSPOSE_SCALE) % AMOUNT_OF_MODES;
            uint8_t scale_intervals[7];
            get_mode_scale(mode, scale_intervals);
            pre_shift_msg.note = snap_note_to_scale(pre_shift_msg.note,
                                                   scale_intervals,
                                                   save_get(TRANSPOSE_BASE_NOTE));
        }

        pipeline_execute_from(next_stage, &pre_shift_msg);

        midi_note shifted_msg = pre_shift_msg;
        midi_pitch_shift(&shifted_msg);
        pipeline_execute_from(next_stage, &shifted_msg);
        return 1;
    }

    midi_pitch_shift(midi_msg);
    return 0;
}

// ---------------------
// Pipeline Arp
// --------------------
static uint8_t pipeline_stage_arp(midi_note *midi_msg, uint8_t length, uint8_t next_stage)
{
    (void)length;
    (void)next_stage;

    if (save_get(ARPEGGIATOR_CURRENTLY_SENDING) == 1 && split_route_allows_menu(MENU_ARPEGGIATOR) == 1) {
        if (arp_handle_midi_cc64(midi_msg) != 0) {
            return 1;
        }

        uint8_t is_note_on = 0;
        if (midi_is_note_message(midi_msg, &is_note_on)) {
            arp_handle_midi_note(midi_msg);
            return 1;
        }
    }

    return 0;
}



// ---------------------
// Final dispatch
// ---------------------
void pipeline_final(midi_note *midi_msg, uint8_t length)
{
    (void)pipeline_stage_final(midi_msg, length, PIPELINE_STAGE_COUNT);
}

static uint8_t pipeline_stage_final(midi_note *midi_msg, uint8_t length, uint8_t next_stage)
{
    (void)next_stage;

    if (split_route_allows_menu(MENU_DISPATCH) == 1 && midi_dispatch_process(midi_msg, length) != 0) {
        return 1;
    }

    emit_midi(midi_msg);
    return 1;
}



static output_route_t midi_route_from_settings(void)
{
    output_route_t route = { .uart1 = 0, .uart2 = 0, .usb = 0 };

    switch (save_get(SETTINGS_SEND_TO_OUT)) {
        case MIDI_OUT_1:
            route.uart1 = 1;
            break;

        case MIDI_OUT_2:
            route.uart2 = 1;
            break;

        case MIDI_OUT_1_2:
            route.uart1 = 1;
            route.uart2 = 1;
            break;

        case MIDI_OUT_SPLIT:
            if (save_get(SPLIT_CURRENTLY_SENDING) == 1) {
                if (g_pipeline_split_target == SPLIT_TARGET_LOW) {
                    route.uart1 = 1;
                } else if (g_pipeline_split_target == SPLIT_TARGET_HIGH) {
                    route.uart2 = 1;
                } else {
                    route.uart1 = 1;
                    route.uart2 = 1;
                }
            } else {
                route.uart1 = 1;
                route.uart2 = 1;
            }
            break;

        default:
            break;
    }

    return route;
}

static void emit_midi_to_route(const midi_note *msg, const output_route_t *route)
{
    if ((msg == NULL) || (route == NULL)) {
        return;
    }
    if (msg->status < 0x80) {
        return;
    }

    const uint8_t length = midi_message_length(msg->status);
    uint8_t bytes[3] = { 0 };
    bytes[0] = msg->status;
    if (length > 1) {
        bytes[1] = msg->note;
    }
    if (length > 2) {
        bytes[2] = msg->velocity;
    }

    if (route->uart1 != 0) {
        HAL_UART_Transmit(&huart1, bytes, length, 1000);
    }
    if (route->uart2 != 0) {
        HAL_UART_Transmit(&huart2, bytes, length, 1000);
    }
    if (route->usb != 0) {
        send_usb_midi_message(bytes, length);
    }
}

void emit_midi(const midi_note *msg)
{
    output_route_t route = midi_route_from_settings();
    const uint8_t usb_mode = (uint8_t)save_get(SETTINGS_SEND_USB);
    if (midi_usb_mode_allows_out(usb_mode) != 0) {
        route.usb = 1;
    }
    emit_midi_to_route(msg, &route);
}
