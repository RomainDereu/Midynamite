/*
 * midi_arp.c
 *
 *  Created on: Feb 21, 2026
 *      Author: Romain Dereu
 */

#include <string.h>

#include "memory_main.h"
#include "midi_arp.h"
#include "threads.h"

typedef struct {
    uint8_t note;
    uint8_t source_note;
} arp_note_entry_t;

typedef struct {
    midi_note physical_notes[ARP_MAX_NOTES];
    midi_note active_notes[ARP_MAX_NOTES];
    uint16_t note_order[ARP_MAX_NOTES];
    uint16_t note_order_counter;
    uint8_t sustain_hold_active;
} arp_input_tracker_t;

typedef struct {
    uint16_t note_index;
    uint8_t note_type;
    uint8_t pattern_step;
    uint32_t random_state;
} arp_pattern_engine_t;

typedef struct {
    uint16_t tick_counter;
    uint16_t gate_tick_counter;
    uint8_t note_on_playing;
    uint8_t currently_playing_count;
    uint8_t has_played_note;
    uint8_t was_arp_enabled;
    uint8_t swing_phase;
    midi_note currently_playing_notes[ARP_MAX_NOTES];
} arp_clock_gate_engine_t;

static arp_input_tracker_t s_input_tracker;
static arp_pattern_engine_t s_pattern_engine;
static arp_clock_gate_engine_t s_clock_gate_engine;

static arp_note_entry_t s_source_entries[ARP_MAX_NOTES];
static arp_note_entry_t s_expanded_entries[ARP_MAX_NOTES];

typedef enum {
    ARP_PATTERN_UP = 0,
    ARP_PATTERN_DOWN,
    ARP_PATTERN_UP_DOWN,
    ARP_PATTERN_UP_DOWN_2,
    ARP_PATTERN_RANDOM,
    ARP_PATTERN_DOUBLE_UP,
    ARP_PATTERN_DOUBLE_DOWN,
    ARP_PATTERN_ORDER = 7,
    ARP_PATTERN_ALL = 8
} arp_pattern_t;

/* ------------------------------- Input tracker ------------------------------- */

static uint8_t input_hold_is_active(void)
{
    return (save_get(ARPEGGIATOR_HOLD) != 0 || s_input_tracker.sustain_hold_active != 0) ? 1 : 0;
}

static uint8_t input_physical_note_count(void)
{
    uint8_t count = 0;

    for (uint8_t note = 0; note < ARP_MAX_NOTES; ++note) {
        count += (s_input_tracker.physical_notes[note].status != 0) ? 1 : 0;
    }

    return count;
}

static void input_sync_note_order_with_active(void)
{
    for (uint8_t note = 0; note < ARP_MAX_NOTES; ++note) {
        if (s_input_tracker.active_notes[note].status != 0) {
            continue;
        }
        s_input_tracker.note_order[note] = 0;
    }
}

static void input_sync_hold_mode(void)
{
    if ((input_hold_is_active() == 0) ||
        (save_get(ARPEGGIATOR_CURRENTLY_SENDING) == 0)) {
        memcpy(s_input_tracker.active_notes,
               s_input_tracker.physical_notes,
               sizeof(s_input_tracker.active_notes));
        input_sync_note_order_with_active();
    }
}



static void input_clear_tracked_notes(void)
{
    memset(s_input_tracker.physical_notes, 0, sizeof(s_input_tracker.physical_notes));
    memset(s_input_tracker.active_notes, 0, sizeof(s_input_tracker.active_notes));
    memset(s_input_tracker.note_order, 0, sizeof(s_input_tracker.note_order));
    s_input_tracker.note_order_counter = 0;
}

static uint8_t input_collect_pressed_keys(arp_note_entry_t *out_entries)
{
    uint8_t count = 0;

    for (uint8_t note = 0; note < ARP_MAX_NOTES; ++note) {
        if (s_input_tracker.active_notes[note].status == 0) {
            continue;
        }

        if ((out_entries != NULL) && (count < ARP_MAX_NOTES)) {
            out_entries[count].note = note;
            out_entries[count].source_note = note;
        }

        ++count;
    }

    return count;
}

static uint8_t input_collect_ordered_keys(arp_note_entry_t *out_entries)
{
    uint8_t count = 0;

    for (uint16_t order = 1; order <= s_input_tracker.note_order_counter; ++order) {
        for (uint8_t note = 0; note < ARP_MAX_NOTES; ++note) {
            if ((s_input_tracker.active_notes[note].status == 0) ||
                (s_input_tracker.note_order[note] != order)) {
                continue;
            }

            if ((out_entries != NULL) && (count < ARP_MAX_NOTES)) {
                out_entries[count].note = note;
                out_entries[count].source_note = note;
            }

            ++count;
        }
    }

    return count;
}

static uint8_t input_expand_entries_across_octaves(const arp_note_entry_t *source_entries, uint8_t source_count)
{
    if (source_count == 0) {
        return 0;
    }

    uint8_t octaves = (uint8_t)save_get(ARPEGGIATOR_OCTAVES);
    if (octaves < 1) {
        octaves = 1;
    }
    if (octaves > 4) {
        octaves = 4;
    }

    uint8_t expanded_count = 0;

    for (uint8_t octave = 0; octave < octaves; ++octave) {
        const uint8_t transpose = (uint8_t)(12 * octave);

        for (uint8_t i = 0; i < source_count; ++i) {
            const uint16_t candidate = (uint16_t)source_entries[i].note + transpose;
            if ((candidate > 127) || (expanded_count >= ARP_MAX_NOTES)) {
                continue;
            }

            s_expanded_entries[expanded_count].note = (uint8_t)candidate;
            s_expanded_entries[expanded_count].source_note = source_entries[i].source_note;
            ++expanded_count;
        }
    }

    return expanded_count;
}

/* ------------------------------- Pattern engine ------------------------------ */

static void pattern_reset_runtime_state(void)
{
    s_pattern_engine.note_index = 0;
}

static uint8_t pattern_index_up(uint8_t count)
{
    return (uint8_t)(s_pattern_engine.note_index % count);
}

static uint8_t pattern_index_down(uint8_t count)
{
    return (uint8_t)((count - 1) - (s_pattern_engine.note_index % count));
}

static uint8_t pattern_index_up_down(uint8_t count)
{
    if (count <= 1) {
        return 0;
    }

    const uint8_t cycle = (uint8_t)((2 * count) - 2);
    const uint8_t pos = (uint8_t)(s_pattern_engine.note_index % cycle);

    if (pos < count) {
        return pos;
    }

    return (uint8_t)(cycle - pos);
}

static uint8_t pattern_index_up_down_2(uint8_t count)
{
    if (count <= 1) {
        return 0;
    }

    const uint8_t cycle = (uint8_t)(2 * count);
    const uint8_t pos = (uint8_t)(s_pattern_engine.note_index % cycle);

    if (pos < count) {
        return pos;
    }

    return (uint8_t)((cycle - 1) - pos);
}

static uint8_t pattern_index_random(uint8_t count)
{
    if (s_pattern_engine.random_state == 0) {
        s_pattern_engine.random_state = 0x9E3779B9;
    }

    /*
     * Standard xorshift32 parameters (13, 17, 5).
     * This keeps state local to the arpeggiator and avoids libc rand() quirks.
     */
    s_pattern_engine.random_state ^= s_pattern_engine.random_state << 13;
    s_pattern_engine.random_state ^= s_pattern_engine.random_state >> 17;
    s_pattern_engine.random_state ^= s_pattern_engine.random_state << 5;

    return (uint8_t)(s_pattern_engine.random_state % count);
}

static uint8_t pattern_index_double_up(uint8_t count)
{
    return (uint8_t)((s_pattern_engine.note_index / 2) % count);
}

static uint8_t pattern_index_double_down(uint8_t count)
{
    const uint8_t down_index = (uint8_t)((s_pattern_engine.note_index / 2) % count);
    return (uint8_t)((count - 1) - down_index);
}

static uint8_t pattern_next_note_index(uint8_t count, uint8_t pattern)
{
    if (count == 0) {
        return 0;
    }

    if (pattern != s_pattern_engine.note_type) {
        s_pattern_engine.note_type = pattern;
        pattern_reset_runtime_state();
    }

    uint8_t index = 0;

    switch (pattern) {
        case ARP_PATTERN_DOWN:
            index = pattern_index_down(count);
            break;

        case ARP_PATTERN_UP_DOWN:
            index = pattern_index_up_down(count);
            break;

        case ARP_PATTERN_UP_DOWN_2:
            index = pattern_index_up_down_2(count);
            break;

        case ARP_PATTERN_RANDOM:
            index = pattern_index_random(count);
            break;

        case ARP_PATTERN_DOUBLE_UP:
            index = pattern_index_double_up(count);
            break;

        case ARP_PATTERN_DOUBLE_DOWN:
            index = pattern_index_double_down(count);
            break;

        case ARP_PATTERN_ORDER:
        case ARP_PATTERN_ALL:
        case ARP_PATTERN_UP:
        default:
            index = pattern_index_up(count);
            break;
    }

    ++s_pattern_engine.note_index;
    return index;
}

static uint8_t pattern_step_is_enabled(void)
{
    uint8_t length = (uint8_t)save_get(ARPEGGIATOR_LENGTH);
    if (length < 1) {
        length = 1;
    }
    if (length > 8) {
        length = 8;
    }

    if (s_pattern_engine.pattern_step >= length) {
        s_pattern_engine.pattern_step = 0;
    }

    const uint8_t step = s_pattern_engine.pattern_step;
    const uint32_t notes_mask = (uint32_t)save_get(ARPEGGIATOR_NOTES);
    const uint8_t enabled = (uint8_t)((notes_mask >> step) & 1);

    s_pattern_engine.pattern_step = (uint8_t)((s_pattern_engine.pattern_step + 1) % length);
    return enabled;
}


/* ------------------------------ Clock/gate engine ---------------------------- */

static uint16_t clocks_per_step(uint8_t div)
{
    static const uint16_t map[10] = {192, 96, 64, 48, 32, 24, 16, 12, 8, 6};

    if (div > 9) {
        div = 9;
    }

    return map[div];
}




static uint16_t clock_current_step_ticks(uint16_t clocks_per_step_value)
{
    uint16_t swing = (uint16_t)(uint8_t)save_get(ARPEGGIATOR_SWING);

    if ((clocks_per_step_value > 100) || (swing >= clocks_per_step_value)) {
        if (swing > 99) {
            swing = 99;
        }
        swing = (uint16_t)((clocks_per_step_value * swing + 50) / 100);
    }

    if (swing < 1) {
        swing = 1;
    }
    if (swing >= clocks_per_step_value) {
        swing = (uint16_t)(clocks_per_step_value - 1);
    }

    if (s_clock_gate_engine.swing_phase == 0) {
        return (uint16_t)(2 * swing);
    }

    return (uint16_t)(2 * (clocks_per_step_value - swing));
}


static uint8_t clock_should_process_step(uint16_t step_ticks_value, uint8_t has_pressed_notes)
{
    if ((save_get(TEMPO_CURRENTLY_SENDING) == 0) &&
        (s_clock_gate_engine.has_played_note == 0) &&
        (has_pressed_notes != 0)) {
        s_clock_gate_engine.tick_counter = 0;
        return 1;
    }

    s_clock_gate_engine.tick_counter++;
    if (s_clock_gate_engine.tick_counter < step_ticks_value) {
        return 0;
    }

    s_clock_gate_engine.tick_counter = 0;
    return 1;
}

static void clock_send_note_off(const midi_note *on_note)
{
    midi_note off = *on_note;
    off.status = (uint8_t)(0x80 | (on_note->status & 0x0F));
    off.velocity = 0;
    pipeline_final(&off, 3);
}

static void clock_send_note_off_and_clear_gate(void)
{
    for (uint8_t i = 0; i < s_clock_gate_engine.currently_playing_count; ++i) {
        clock_send_note_off(&s_clock_gate_engine.currently_playing_notes[i]);
    }

    s_clock_gate_engine.note_on_playing = 0;
    s_clock_gate_engine.currently_playing_count = 0;
    s_clock_gate_engine.gate_tick_counter = 0;
}

static void clock_process_gate(uint16_t gate_ticks)
{
    if (s_clock_gate_engine.note_on_playing == 0) {
        return;
    }

    s_clock_gate_engine.gate_tick_counter++;
    if (s_clock_gate_engine.gate_tick_counter < gate_ticks) {
        return;
    }

    clock_send_note_off_and_clear_gate();
}

static void clock_stop_playing_note(void)
{
    if (s_clock_gate_engine.note_on_playing == 0) {
        return;
    }

    clock_send_note_off_and_clear_gate();
}


/* ------------------------------- Coordination -------------------------------- */

static void arp_apply_key_sync_on_note_on(uint8_t note)
{
    if (save_get(ARPEGGIATOR_KEY_SYNC) == 0) {
        return;
    }

    if (s_input_tracker.physical_notes[note].status != 0) {
        return;
    }

    s_clock_gate_engine.has_played_note = 0;
    s_clock_gate_engine.tick_counter = 0;
    s_clock_gate_engine.swing_phase = 0;
    pattern_reset_runtime_state();
    s_pattern_engine.note_type = 255;
}



static void arp_clear_tracked_notes(void)
{
    input_clear_tracked_notes();

    s_clock_gate_engine.has_played_note = 0;
    s_clock_gate_engine.tick_counter = 0;
    s_clock_gate_engine.swing_phase = 0;

    s_pattern_engine.pattern_step = 0;
    pattern_reset_runtime_state();
    s_pattern_engine.note_type = 255;
}

void arp_panic_clear(void)
{
    clock_stop_playing_note();
    arp_clear_tracked_notes();
}


void arp_handle_midi_note(const midi_note *msg)
{
    uint8_t is_note_on = 0;

    if (!midi_is_note_message(msg, &is_note_on)) {
        return;
    }

    input_sync_hold_mode();

    const uint8_t note = (uint8_t)(msg->note & 0x7F);
    const uint8_t incoming_channel = (uint8_t)(msg->status & 0x0F);
    const uint8_t hold_enabled = input_hold_is_active();

    if (is_note_on) {
        arp_apply_key_sync_on_note_on(note);

        if ((hold_enabled != 0) && (input_physical_note_count() == 0)) {
            memset(s_input_tracker.active_notes, 0, sizeof(s_input_tracker.active_notes));
            memset(s_input_tracker.note_order, 0, sizeof(s_input_tracker.note_order));
            s_input_tracker.note_order_counter = 0;
            s_clock_gate_engine.has_played_note = 0;
            pattern_reset_runtime_state();
            s_pattern_engine.note_type = 255;
        }

        if (s_input_tracker.physical_notes[note].status == 0 ||
            (uint8_t)(s_input_tracker.physical_notes[note].status & 0x0F) == incoming_channel) {
            s_input_tracker.physical_notes[note] = *msg;
            s_input_tracker.physical_notes[note].note = note;
        }

        if (s_input_tracker.active_notes[note].status == 0 ||
            (uint8_t)(s_input_tracker.active_notes[note].status & 0x0F) == incoming_channel) {
            s_input_tracker.active_notes[note] = *msg;
            s_input_tracker.active_notes[note].note = note;
            s_input_tracker.note_order[note] = ++s_input_tracker.note_order_counter;
        }

        return;
    }

    if ((s_input_tracker.physical_notes[note].status != 0) &&
        ((uint8_t)(s_input_tracker.physical_notes[note].status & 0x0F) == incoming_channel)) {
        memset(&s_input_tracker.physical_notes[note], 0, sizeof(s_input_tracker.physical_notes[note]));
    }

    if ((hold_enabled == 0) &&
        (s_input_tracker.active_notes[note].status != 0) &&
        ((uint8_t)(s_input_tracker.active_notes[note].status & 0x0F) == incoming_channel)) {
        memset(&s_input_tracker.active_notes[note], 0, sizeof(s_input_tracker.active_notes[note]));
        s_input_tracker.note_order[note] = 0;
    }
}

uint8_t arp_handle_midi_cc64(const midi_note *msg)
{
    if (msg == NULL) {
        return 0;
    }

    const uint8_t status_nibble = (uint8_t)(msg->status & 0xF0);
    if ((status_nibble != 0xB0) || ((msg->note & 0x7F) != 64)) {
        return 0;
    }

    s_input_tracker.sustain_hold_active = (msg->velocity >= 64) ? 1 : 0;
    input_sync_hold_mode();

    if (save_get(ARPEGGIATOR_CURRENTLY_SENDING) == 1) {
        return 1;
    }
    return 0;
}

void arp_on_tempo_tick(void)
{
    input_sync_hold_mode();

    const uint8_t arp_enabled = (save_get(ARPEGGIATOR_CURRENTLY_SENDING) != 0) ? 1 : 0;
    if ((arp_enabled == 0) && (s_clock_gate_engine.was_arp_enabled != 0)) {
        clock_stop_playing_note();
        arp_clear_tracked_notes();
    }
    s_clock_gate_engine.was_arp_enabled = arp_enabled;

    if (save_get(ARPEGGIATOR_CURRENTLY_SENDING) == 0) {
        return;
    }

    const uint8_t pattern = (uint8_t)save_get(ARPEGGIATOR_PATTERN);
    const uint8_t source_count =
        (pattern == ARP_PATTERN_ORDER)
            ? input_collect_ordered_keys(s_source_entries)
            : input_collect_pressed_keys(s_source_entries);

    const uint8_t count = input_expand_entries_across_octaves(s_source_entries, source_count);

    const uint16_t cps = clocks_per_step((uint8_t)save_get(ARPEGGIATOR_DIVISION));
    const uint16_t gate_ticks = (uint16_t)(((cps * save_get(ARPEGGIATOR_GATE)) + 5) / 10);
    const uint16_t step_ticks = clock_current_step_ticks(cps);

    clock_process_gate(gate_ticks);

    if (!clock_should_process_step(step_ticks, count)) {
        return;
    }

    if (s_clock_gate_engine.note_on_playing != 0) {
        clock_stop_playing_note();
    }

    if (count == 0) {
        s_clock_gate_engine.has_played_note = 0;
        s_pattern_engine.pattern_step = 0;
        pattern_reset_runtime_state();
        s_pattern_engine.note_type = 255;
        s_clock_gate_engine.swing_phase ^= 1;
        return;
    }

    if (pattern_step_is_enabled() == 0) {
        s_clock_gate_engine.swing_phase ^= 1;
        return;
    }

    if (pattern == ARP_PATTERN_ALL) {
        s_clock_gate_engine.currently_playing_count = 0;

        for (uint8_t i = 0; i < count; ++i) {
            const arp_note_entry_t selected_entry = s_expanded_entries[i];
            const midi_note source_msg = s_input_tracker.active_notes[selected_entry.source_note];
            midi_note on = source_msg;

            on.status = (uint8_t)(0x90 | (source_msg.status & 0x0F));
            on.note = selected_entry.note;
            pipeline_final(&on, 3);

            if (s_clock_gate_engine.currently_playing_count < ARP_MAX_NOTES) {
                s_clock_gate_engine.currently_playing_notes[s_clock_gate_engine.currently_playing_count] = on;
                ++s_clock_gate_engine.currently_playing_count;
            }
        }
    } else {
        const uint8_t index = pattern_next_note_index(count, pattern);
        const arp_note_entry_t selected_entry = s_expanded_entries[index];
        const midi_note source_msg = s_input_tracker.active_notes[selected_entry.source_note];

        midi_note on = source_msg;
        on.status = (uint8_t)(0x90 | (source_msg.status & 0x0F));
        on.note = selected_entry.note;
        pipeline_final(&on, 3);

        s_clock_gate_engine.currently_playing_notes[0] = on;
        s_clock_gate_engine.currently_playing_count = 1;
    }

    s_clock_gate_engine.has_played_note = (s_clock_gate_engine.currently_playing_count > 0) ? 1 : 0;
    s_clock_gate_engine.note_on_playing = (s_clock_gate_engine.currently_playing_count > 0) ? 1 : 0;
    s_clock_gate_engine.gate_tick_counter = 0;
    s_clock_gate_engine.swing_phase ^= 1;
}

#ifdef UNIT_TEST
/* Used for tests */


void arp_state_reset(void)
{
    memset(&s_input_tracker, 0, sizeof(s_input_tracker));
    memset(&s_pattern_engine, 0, sizeof(s_pattern_engine));
    s_pattern_engine.note_type = 255;
    s_pattern_engine.random_state = 0x9E3779B9;
    memset(&s_clock_gate_engine, 0, sizeof(s_clock_gate_engine));

}

uint8_t arp_get_pressed_keys(uint8_t *out_notes)
{
    const uint8_t count = input_collect_pressed_keys(s_source_entries);

    if (out_notes != NULL) {
        for (uint8_t i = 0; i < count; ++i) {
            out_notes[i] = s_source_entries[i].note;
        }
    }

    return count;
}

uint8_t arp_get_pressed_key_count(void)
{
    return input_collect_pressed_keys(NULL);
}

void arp_sync_hold_mode(void)
{
    input_sync_hold_mode();
}

#endif
