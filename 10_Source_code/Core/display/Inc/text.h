/*
 * text.h
 *
 *  Created on: Jun 30, 2025
 *      Author: Romain Dereu
 */

#ifndef TEXT_H_
#define TEXT_H_

// Central struct for all UI strings and selectable options
typedef struct {
    // Menu Titles
    const char *send_midi_tempo;
    const char *target;
    const char *settings_modify;
    const char *settings_transpose;
    const char *global_settings_1;
    const char *global_settings_2;

    const char *about;
    const char *midi_modify;
    const char *midi_split;
    const char *midi_transpose;
    const char *midi_dispatch;

    // Settings
    const char *MIDI_Thru_;
    const char *MIDI_Out_;
    const char *MIDI_Filter_;

    const char *equals_ignore_channel;


    // Channel Modify
    const char *midi_modify_select;
    const char *send_1_sem;
    const char *send_2_sem;

    // Channel Split
    const char *dry_buses_all[4];
    const char *split_note;
    const char *split_ch;
    const char *split_velocity;
    const char *low_sem;
    const char *high_sem;
    const char *low_to;
    const char *high_to;
    const char *ch_sem;
    const char *select_bus_effects;
    const char *modify_short;
    const char *transpose_short;
    const char *arpeggiator_short;
    const char *dispatch_short;
    const char *split_types[3];
    const char *bus_effect_mask[4];

    // Velocity
    const char *velocity;
    const char *change;
    const char *fixed;
    const char *change_velocity;
    const char *fixed_velocity;


    // Transpose
    const char *type;
    const char *pitch_shift;
    const char *mode;
    const char *na;
    const char *send_base_note;
    const char *interval;
    const char *root_note;

    const char *scale;
    const char *send_base;
    const char *shift_by;
    const char *semitones;

    // Modes
    const char *ionian;
    const char *dorian;
    const char *phrygian;
    const char *lydian;
    const char *mixolydian;
    const char *mixo;
    const char *aeolian;
    const char *locrian;

    // Intervals
    const char *octave_dn;
    const char *sixth_dn;
    const char *fifth_dn;
    const char *fourth_dn;
    const char *third_dn;
    const char *third_up;
    const char *fourth_up;
    const char *fifth_up;
    const char *sixth_up;
    const char *octave_up;

    const char *send_to;


    // Start Menu
    const char *start_menu_;


    // Contrast
    const char *contrast_;
    const char *ten_hundred_ten_percent[11];
    const char *zero_hundred_ten[11];


    // About
    const char *about_brand;
    const char *about_product;
    const char *about_version;

    // Saving
    const char *save_instruction;
    const char *saving;
    const char *saved;

    // Booleans
    const char *on;
    const char *off;
    const char *yes;
    const char *no;

    // MIDI Tempo
    const char *bpm;
    const char *tempo;

    //USB Midi
    const char *usb_midi_;


    //Upgrade mode
    const char *upgrade_mode;

    // Arpeggiator
    const char *arpeggiator_1;
    const char *arpeggiator_2;
    const char *pattern_;
    const char *division_;
    const char *gate_;
    const char *hold_;
    const char *steps_;
    const char *swing_;
    const char *length_;
    const char *key_sync_;

	//Dispatch
    const char *synths_;
    const char *from_ch_;
    const char *notes_per_synth_;
    const char *voice_delete_;
    const char *voice_manage_options[4];

    //Error handlers
    const char *error;


    // Note names
    const char *midi_note_names[128];
    const char *neg_pos_80[161];
    const char *zer_to_300[301];
    const char *twelve_notes_names[12];

    const char *zero_to_sixteen[17];
    const char *one_to_sixteen_one_char[17];

    const char *midi_outs[3];
    const char *midi_outs_split[4];
    const char *transpose_modes[2];
    const char *scales[7];
    const char *intervals[10];
    const char *no_yes[2];
    const char *off_on[2];
    const char *off_out_thru_thru_both[4];

    const char *midi_channels[18];
    const char *menu_list[7];
    const char *menu_list_long[7];


    const char *division_list[10];
    const char *arp_patterns[9];
    const char *octave_count[5];


} Message;
// Global access to UI strings and options
extern const Message *message;

#endif /* TEXT_H_ */
