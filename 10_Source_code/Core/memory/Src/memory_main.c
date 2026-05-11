/*
 * memory_main.c
 *
 *  Created on: Aug 23, 2025
 *      Author: Romain Dereu
 */
#include "memory_main.h"
#include "_menu_controller.h" // for menu_controls[], s_field_change_bits (mark_field_changed)

// Expose for tests
const save_limits_t save_limits[SAVE_FIELD_COUNT] = {
    //                                   min         max         default
    [TEMPO_CURRENT_TEMPO]        = {     20,        300,        120 },
    [TEMPO_CURRENTLY_SENDING]    = {      0,          1,          0 },
    [TEMPO_SEND_TO_MIDI_OUT]     = {      0,          2,          0 },



	[SPLIT_MIDI_CH1]             = {      0,         16,          0 },
	[SPLIT_MIDI_CH2]             = {      0,         16,          0 },
	[SPLIT_SEND_CH1]             = {      0,         3,           3 },
	[SPLIT_SEND_CH2]             = {      0,         3,           3 },

	[SPLIT_TYPE]                 = {      0,        2,            0 },
	[SPLIT_NOTE]                 = {      0,        127,         60 },
	[SPLIT_MIDI_CHANNEL]         = {      2,        16,           8 },
	[SPLIT_VELOCITY]             = {      2,        127,          60 },
	[SPLIT_MENU_MASK]            = {      0,        255,        255 },

	[SPLIT_MASK_MODIFY]          = {      1,         3,           3 },
	[SPLIT_MASK_TRANSPOSE]       = {      1,         3,           3 },
	[SPLIT_MASK_ARPEGGIATOR]     = {      1,         3,           3 },
	[SPLIT_MASK_DISPATCH]        = {      1,         3,           3 },


	[SPLIT_CURRENTLY_SENDING]    = {      0,          1,          0 },



	[MODIFY_VELOCITY_TYPE]       = {      0,          1,          0 },

    [MODIFY_SEND_TO_MIDI_CH1]    = {      1,         17,          1 },
    [MODIFY_SEND_TO_MIDI_CH2]    = {      0,         17,          0 },


    [MODIFY_VEL_PLUS_MINUS]      = {    -80,         80,          0 },
    [MODIFY_VEL_ABSOLUTE]        = {      0,        127,         64 },

    [MODIFY_CURRENTLY_SENDING]   = {      0,          1,          0 },



    [TRANSPOSE_TRANSPOSE_TYPE]   = {      0,          1,          0 },
    [TRANSPOSE_MIDI_SHIFT_VALUE] = {    -36,         36,          0 },
    [TRANSPOSE_BASE_NOTE]        = {      0,         11,          0 },
    [TRANSPOSE_INTERVAL]         = {      0,          9,          0 },
    [TRANSPOSE_TRANSPOSE_SCALE]  = {      0,          6,          0 },
    [TRANSPOSE_SEND_ORIGINAL]    = {      0,          1,          0 },
    [TRANSPOSE_CURRENTLY_SENDING]= {      0,          1,          0 },



	[ARPEGGIATOR_CURRENTLY_SENDING]= {      0,       1,          0 },
	[ARPEGGIATOR_DIVISION]         = {      0,       9,          3 },
	[ARPEGGIATOR_GATE]             = {      1,       10,         10 },
	[ARPEGGIATOR_OCTAVES]          = {      1,       4,          1 },
	[ARPEGGIATOR_PATTERN]          = {      0,       8,          0 },

	[ARPEGGIATOR_SWING]            = {      1,       100,        50 },
	[ARPEGGIATOR_LENGTH]           = {      1,       8,          8 },
	[ARPEGGIATOR_NOTES]            = {      0,       0b11111111, 0b11111111 },
	[ARPEGGIATOR_HOLD]             = {      0,       1,          0 },
	[ARPEGGIATOR_KEY_SYNC]         = {      0,       1,          0 },

	[DISPATCH_CURRENTLY_SENDING]   = {      0,       1,          0 },
	[DISPATCH_AMOUNT_OF_SYNTHS]    = {      1,      16,          4},
	[DISPATCH_FROM_CHANNEL]        = {      1,      16,          1 },
	[DISPATCH_NOTES_PER_SYNTH]     = {      1,       6,          1 },
	[DISPATCH_VOICE_MANAGE]        = {      0,       3,          0 },


    [SETTINGS_START_MENU]        = {      0,          AMOUNT_OF_MENUS-1,          0 },
    [SETTINGS_SEND_USB]          = {      0,          3,          0 },
    [SETTINGS_BRIGHTNESS]        = {      1,          10,         7 },
    [SETTINGS_MIDI_THRU]         = {      0,          1,          0 },
	[SETTINGS_SEND_TO_OUT]       = {      0,          3,          0 },
    [SETTINGS_CHANNEL_FILTER]    = {      0,          1,          0 },
    [SETTINGS_FILTERED_CH]       = {      0,  0x0000FFFF,         0 },
    [SETTINGS_ABOUT]             = {      0,          0,          0 },



    [SAVE_DATA_VALIDITY]         = {      0,  0xFFFFFFFF, DATA_VALIDITY_CHECKSUM },
};

static volatile uint8_t save_busy = 0;

// ---------------------
// Lock helpers
// ---------------------
static int save_try_lock(void) {
    if (save_busy) return 0;
    save_busy = 1;
    return 1;
}

void save_unlock(void) { save_busy = 0; }

// (Optional) bounded retry for writers; cheap and unit-test friendly
uint8_t save_lock_with_retries(void) {
    for (int i = 0; i < 5; i++) {
        if (save_try_lock()) return 1;
        // short, bounded spin
        volatile int spin = 200;
        while (spin--) { /* no-op */ }
    }
    return 0;
}


// ---------------------
// Generic getters (BUSY-safe)
// ---------------------
// Small bounded wait helper used by getters (non-blocking feel)
static inline void save_busy_wait_short(void) {
    // ~ a few hundred no-ops; tweak if needed
    volatile int spin = 200;
    while (spin--) { /* no-op */ }
}

static int32_t save_get_u32(save_field_t field) {
    int32_t *p = u32_fields[field];
    if (!p) return 0;

    for (int i = 0; i < 5; ++i) {
        if (!save_busy) break;
        save_busy_wait_short();
    }

    return *p;
}

static uint8_t split_mask_field_to_index(save_field_t field)
{
    switch (field) {
        case SPLIT_MASK_MODIFY:      return 0;
        case SPLIT_MASK_TRANSPOSE:   return 1;
        case SPLIT_MASK_ARPEGGIATOR: return 2;
        case SPLIT_MASK_DISPATCH:    return 3;
        default:                     return 0xFF;
    }
}

static uint8_t split_mask_get_slot_value(save_field_t field)
{
    const uint8_t idx = split_mask_field_to_index(field);
    if (idx == 0xFF) return 0;

    const uint32_t mask = (uint32_t)save_get(SPLIT_MENU_MASK);
    return (uint8_t)((mask >> (idx * 2)) & 0x03);
}

static uint8_t split_mask_set_slot_value(save_field_t field, uint8_t value)
{
    const uint8_t idx = split_mask_field_to_index(field);
    if (idx == 0xFF) return 0;

    uint8_t new_value = value;
    if (new_value < 1) new_value = 1;
    if (new_value > 3) new_value = 3;

    uint32_t mask = (uint32_t)save_get(SPLIT_MENU_MASK);
    const uint32_t shift = (uint32_t)(idx * 2);
    const uint32_t clear_slot = ~(0x03UL << shift);
    mask = (mask & clear_slot) | (((uint32_t)new_value & 0x03UL) << shift);
    return save_modify_u32(SPLIT_MENU_MASK, SAVE_MODIFY_SET, mask);
}

static uint8_t save_get_8(save_field_t field) {
    uint8_t *p = u8_fields[field];
    if (!p) return 0;

    for (int i = 0; i < 5; ++i) {
        if (!save_busy) break;
        save_busy_wait_short();
    }

    return *p;
}


int32_t save_get(save_field_t field) {
    if (field >= SPLIT_MASK_MODIFY && field <= SPLIT_MASK_DISPATCH) {
        return (int32_t)split_mask_get_slot_value(field);
    }

    // Prefer exact width based on backing storage
    if (u8_fields[field])  return (int32_t)save_get_8(field);
    if (u32_fields[field]) return        save_get_u32(field);

    return 0;
}


// ---------------------
// Increment / set (u32 / u8)
// ---------------------
static void mark_field_changed(save_field_t f) {
    if ((unsigned)f >= SAVE_FIELD_COUNT) return;
    s_field_change_bits[f >> 5] |= ((uint32_t)1 << (f & 31));
}

// Utils: wrap/clamp a value into [min, max] with optional wrap
int32_t wrap_or_clamp_i32(int32_t v, int32_t min, int32_t max, uint8_t wrap)
{
    if (min > max) { int32_t t = min; min = max; max = t; }

    if (wrap == NO_WRAP) {
        if (v < min) return min;
        if (v > max) return max;
        return v;
    }

    // Inclusive span so [1..16] reaches 16
    const int32_t span = (max - min) + 1;
    if (span <= 0) return min;

    int32_t off = (v - min) % span;
    if (off < 0) off += span;
    return min + off;
}


uint8_t save_modify_u32(save_field_t field, save_modify_op_t op, uint32_t value_if_set) {
    if (field < 0 || field >= SAVE_FIELD_COUNT) return 0;
    if (!u32_fields[field]) return 0;
    if (!save_lock_with_retries()) return 0;

    const save_limits_t   lim = save_limits[field];
    const menu_controls_t mt  = menu_controls[field];
    int32_t old_v = *u32_fields[field];
    int32_t v = old_v;

    switch (op) {
        case SAVE_MODIFY_INCREMENT: v += 1; break;
        case SAVE_MODIFY_SET:       v  = (int32_t)value_if_set; break;
        default: save_unlock(); return 0;
    }

    v = wrap_or_clamp_i32(v, lim.min, lim.max, mt.wrap);

    if (v != old_v) {
        *u32_fields[field] = v;
        mark_field_changed(field);
    }

    save_unlock();
    return 1;
}

uint8_t save_modify_u8(save_field_t field, save_modify_op_t op, uint8_t value_if_set) {
    if (field < 0 || field >= SAVE_FIELD_COUNT) return 0;

    if (field >= SPLIT_MASK_MODIFY && field <= SPLIT_MASK_DISPATCH) {
        const save_limits_t lim = save_limits[field];
        const menu_controls_t mt = menu_controls[field];

        if (op == SAVE_MODIFY_INCREMENT) {
            uint8_t cur = split_mask_get_slot_value(field);
            cur = (cur >= 3) ? 1 : (uint8_t)(cur + 1);
            return split_mask_set_slot_value(field, cur);
        }

        if (op == SAVE_MODIFY_SET) {
            int32_t v = (int32_t)value_if_set;
            if (v > 230) {
                v = (mt.wrap == WRAP) ? lim.max : lim.min;
            }
            v = wrap_or_clamp_i32(v, lim.min, lim.max, mt.wrap);
            return split_mask_set_slot_value(field, (uint8_t)v);
        }

        return 0;
    }


    if (!u8_fields[field]) return 0;
    if (!save_lock_with_retries()) return 0;

    const save_limits_t   lim = save_limits[field];
    const menu_controls_t mt  = menu_controls[field];

    uint8_t old_v = *u8_fields[field];
    int32_t v = (int32_t)old_v;

    switch (op) {
        case SAVE_MODIFY_SET: {
            int32_t desired = (int32_t)value_if_set;
            if (desired > 230) {//For warps, 255 + 25 buffer for button presses
                v = (mt.wrap == WRAP) ? lim.max : lim.min;
            } else {
                v = desired;
            }
        } break;

        case SAVE_MODIFY_INCREMENT:
            v += 1;
            break;

        default:
            save_unlock();
            return 0;
    }

    v = wrap_or_clamp_i32(v, lim.min, lim.max, mt.wrap);
    uint8_t new_v = (uint8_t)v;

    if (new_v != old_v) {
        *u8_fields[field] = new_v;
        mark_field_changed(field);
    }

    save_unlock();
    return 1;
}
