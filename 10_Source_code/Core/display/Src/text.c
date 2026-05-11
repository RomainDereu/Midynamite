/*
 * text.c
 *
 *  Created on: Jun 30, 2025
 *      Author: Romain Dereu
 */

#include <string.h>
#include "text.h"

static const Message _message = {
    // Menu Titles
    .send_midi_tempo = "Send MIDI Tempo",
    .target = "Target",
    .settings_modify = "Settings Midi Modify",
    .settings_transpose = "Settings Transpose",
    .global_settings_1 = "Global Settings 1",
    .global_settings_2 = "Global Settings 2",
    .about = "About",
    .midi_modify = "MIDI Modify",
    .midi_split = "MIDI Split 1/2",
    .midi_transpose = "MIDI Transpose",
    .midi_dispatch = "MIDI Dispatch",

	.MIDI_Thru_ = "MIDI Thru:",
	.MIDI_Out_ = "MIDI Out:",
	.MIDI_Filter_ = "MIDI Filter:",

	.equals_ignore_channel = "0 = Ignore Channel",

    // Channel Modify
    .midi_modify_select = "Ch. Modify",
	.send_1_sem = "Send 1:",
	.send_2_sem = "Send 2:",
	.send_to = "Send to",

    // Channel Split
    .dry_buses_all = { "Dry", "Bus 1", "Bus 2", "All" },
    .split_note = "Split Note",
    .split_ch = "Split Ch.",
    .split_velocity = "Split Vel.",
    .low_sem = "Low:",
    .high_sem = "High:",
	.low_to = "Low to:",
	.high_to = "High to:",
	.ch_sem = "Ch:",
	.select_bus_effects = "Select BUS effects",
	.modify_short = "Modify:",
	.transpose_short = "Transpose:",
	.arpeggiator_short = "Arpeggiator:",
	.dispatch_short = "Dispatch:",
	.split_types = { "Note", "MIDI Ch.", "Velocity" },
	.bus_effect_mask = { "Err", "Bus 1", "Bus 2", "All" },

    // Velocity
    .velocity = "Velocity",
    .change = "Change",
    .fixed = "Fixed",
    .change_velocity = "Change Velocity",
    .fixed_velocity = "Fixed Velocity",



    // Transpose
    .type = "Type",
    .pitch_shift = "Pitch Shift",

    .mode = "Mode",
    .na = "N/A",
    .send_base_note = "Send Base Note",
    .interval = "Interval",
    .root_note = "Root Note",
    .scale = "Scale",
    .send_base = "Send Base",
    .shift_by = "Shift By",
    .semitones = "Semitones",

    // Modes
    .ionian = "Ionian",
    .dorian = "Dorian",
    .phrygian = "Phrygian",
    .lydian = "Lydian",
    .mixolydian = "Mixolydian",
    .mixo = "Mixo",
    .aeolian = "Aeolian",
    .locrian = "Locrian",

    // Intervals
    .octave_dn = "Oct Dn",
    .sixth_dn = "6th Dn",
    .fifth_dn = "5th Dn",
    .fourth_dn = "4th Dn",
    .third_dn = "3rd Dn",
    .third_up = "3rd Up",
    .fourth_up = "4th Up",
    .fifth_up = "5th Up",
    .sixth_up = "6th Up",
    .octave_up = "Oct Up",

    // MIDI Out
    .send_to = "Send to",


    // Start Menu
    .start_menu_= "Start Menu:",


    // Contrast
    .contrast_ = "Contrast:",
    .ten_hundred_ten_percent = { " 0%",
        "10%", "20%", "30%", "40%", "50%",
        "60%", "70%", "80%", "90%", "100%"
    },
	.zero_hundred_ten = { " 0", " 10", " 20", " 30" , " 40", " 50", " 60", " 70", " 80" , " 90", "100"},


    // About
    .about_brand = "Romagnetics",
    .about_product = "Midynamite",
    .about_version = "v2.0.0",

    // Saving
    .save_instruction = "Press select to Save",
    .saving = "Saving...           ",
    .saved = "Saved!              ",

    // Booleans
    .yes = "Yes",
    .no = "No",

    // MIDI Tempo
    .bpm = "BPM",
    .tempo = "Tempo",

    //USB Midi
	.usb_midi_ = "USB Midi:",


    //USB Midi
	.upgrade_mode = "Upgrade Mode",

	//Arpeggio
    .arpeggiator_1 = "Arpeggiator 1/2",
    .arpeggiator_2 = "Arpeggiator 2/2",
	.division_ = "Division:",
	.pattern_ = "Pattern:",
	.gate_ = "Gate:",
	.hold_ = "Hold:",
    .steps_ = "Steps:",
	.swing_ = "Swing:",
	.length_ = "Length:",
	.key_sync_ = "KeySync:",

	//Dispatch
	.synths_ = "Synths:",
	.from_ch_ =  "From Ch:",
	.notes_per_synth_ =  "Notes/synth:",
	.voice_delete_ = "Voice delete:",
	.voice_manage_options = {"First", "Last", "Veloc.", "Random"},


	//Error Handlers
	.error = "ERROR",

    // MIDI Note Names (C-1 to G9)
    .midi_note_names = {
        "C-1", "C#-1", "D-1", "D#-1", "E-1", "F-1", "F#-1", "G-1", "G#-1", "A-1", "A#-1", "B-1",
        "C0", "C#0", "D0", "D#0", "E0", "F0", "F#0", "G0", "G#0", "A0", "A#0", "B0",
        "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1",
        "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
        "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
        "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
        "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5",
        "C6", "C#6", "D6", "D#6", "E6", "F6", "F#6", "G6", "G#6", "A6", "A#6", "B6",
        "C7", "C#7", "D7", "D#7", "E7", "F7", "F#7", "G7", "G#7", "A7", "A#7", "B7",
        "C8", "C#8", "D8", "D#8", "E8", "F8", "F#8", "G8", "G#8", "A8", "A#8", "B8",
        "C9", "C#9", "D9", "D#9", "E9", "F9", "F#9", "G9"
    },

	//For midi_transpose
	.neg_pos_80 = {
	    "-80", "-79", "-78", "-77", "-76", "-75", "-74", "-73", "-72", "-71",
	    "-70", "-69", "-68", "-67", "-66", "-65", "-64", "-63", "-62", "-61",
	    "-60", "-59", "-58", "-57", "-56", "-55", "-54", "-53", "-52", "-51",
	    "-50", "-49", "-48", "-47", "-46", "-45", "-44", "-43", "-42", "-41",
	    "-40", "-39", "-38", "-37", "-36", "-35", "-34", "-33", "-32", "-31",
	    "-30", "-29", "-28", "-27", "-26", "-25", "-24", "-23", "-22", "-21",
	    "-20", "-19", "-18", "-17", "-16", "-15", "-14", "-13", "-12", "-11",
	    "-10", "-9",  "-8",  "-7",  "-6",  "-5",  "-4",  "-3",  "-2",  "-1",
	    "0",
	    "+1",  "+2",  "+3",  "+4",  "+5",  "+6",  "+7",  "+8",  "+9",  "+10",
	    "+11", "+12", "+13", "+14", "+15", "+16", "+17", "+18", "+19", "+20",
	    "+21", "+22", "+23", "+24", "+25", "+26", "+27", "+28", "+29", "+30",
	    "+31", "+32", "+33", "+34", "+35", "+36", "+37", "+38", "+39", "+40",
	    "+41", "+42", "+43", "+44", "+45", "+46", "+47", "+48", "+49", "+50",
	    "+51", "+52", "+53", "+54", "+55", "+56", "+57", "+58", "+59", "+60",
	    "+61", "+62", "+63", "+64", "+65", "+66", "+67", "+68", "+69", "+70",
	    "+71", "+72", "+73", "+74", "+75", "+76", "+77", "+78", "+79", "+80"
	},


	//Numbers for the tempo
	.zer_to_300 = {
	    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	    "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
	    "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
	    "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
	    "40", "41", "42", "43", "44", "45", "46", "47", "48", "49",
	    "50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
	    "60", "61", "62", "63", "64", "65", "66", "67", "68", "69",
	    "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
	    "80", "81", "82", "83", "84", "85", "86", "87", "88", "89",
	    "90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
	    "100", "101", "102", "103", "104", "105", "106", "107", "108", "109",
	    "110", "111", "112", "113", "114", "115", "116", "117", "118", "119",
	    "120", "121", "122", "123", "124", "125", "126", "127", "128", "129",
	    "130", "131", "132", "133", "134", "135", "136", "137", "138", "139",
	    "140", "141", "142", "143", "144", "145", "146", "147", "148", "149",
	    "150", "151", "152", "153", "154", "155", "156", "157", "158", "159",
	    "160", "161", "162", "163", "164", "165", "166", "167", "168", "169",
	    "170", "171", "172", "173", "174", "175", "176", "177", "178", "179",
	    "180", "181", "182", "183", "184", "185", "186", "187", "188", "189",
	    "190", "191", "192", "193", "194", "195", "196", "197", "198", "199",
	    "200", "201", "202", "203", "204", "205", "206", "207", "208", "209",
	    "210", "211", "212", "213", "214", "215", "216", "217", "218", "219",
	    "220", "221", "222", "223", "224", "225", "226", "227", "228", "229",
	    "230", "231", "232", "233", "234", "235", "236", "237", "238", "239",
	    "240", "241", "242", "243", "244", "245", "246", "247", "248", "249",
	    "250", "251", "252", "253", "254", "255", "256", "257", "258", "259",
	    "260", "261", "262", "263", "264", "265", "266", "267", "268", "269",
	    "270", "271", "272", "273", "274", "275", "276", "277", "278", "279",
	    "280", "281", "282", "283", "284", "285", "286", "287", "288", "289",
	    "290", "291", "292", "293", "294", "295", "296", "297", "298", "299",
	    "300"
	},

    // 12 Notes (for scales, transposition etc.)
    .twelve_notes_names = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    },

	.zero_to_sixteen = {
			"/",  "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"
	},

	//Used by 6_82
	.one_to_sixteen_one_char = {
	    "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G"
	},



    .midi_outs = {"Out 1", "Out 2", "Out 1+2"},
    .midi_outs_split = {"Out 1", "Out 2", "Out 1+2", "Split"},
    .transpose_modes = { "Pitch Shift", "Scale" },
    .scales = { "Ionian", "Dorian", "Phrygian", "Lydian", "Mixo.", "Aeolian", "Locrian" },
    .intervals = {
         "Oct Dn", "6th Dn", "5th Dn", "4th Dn", "3rd Dn",
         "3rd Up", "4th Up", "5th Up", "6th Up", "Oct Up"
     },
    .no_yes = { "No", "Yes" },
    .off_on = { "Off", "On" },
	.off_out_thru_thru_both = { "Off", "Out", "Thru", "Thru+Out" },

    .midi_channels = {
         "Off", "Thru",
         "Ch. 1",  "Ch. 2",  "Ch. 3",  "Ch. 4",
         "Ch. 5",  "Ch. 6",  "Ch. 7",  "Ch. 8",
         "Ch. 9",  "Ch. 10", "Ch. 11", "Ch. 12",
         "Ch. 13", "Ch. 14", "Ch. 15", "Ch. 16"
     },
	.menu_list = { "Tempo", "Split", "Modify", "Transpose", "Arp." , "Dispatch", "Settings"},
	.menu_list_long = { "Send Tempo",
			            "MIDI Split",
					    "MIDI Modify",
						"Transpose",
						"Arpeggiator" ,
						"MIDI Dispatch",
						"Settings"},
	//Arpeggio
	.division_list = { "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/24", "1/32" },
	.arp_patterns = { "Up", "Down", "Up/Down", "Up/Down 2", "Random", "Double Up", "Double Down", "Order", "Block"},
	.octave_count = { "Error", "1 Octave", "2 Octaves", "3 Octaves", "4 Octaves"},
};

const Message *message = &_message;
