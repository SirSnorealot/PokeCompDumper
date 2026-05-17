#include "pokescript.h"
#include "poketext.h"

#include <QFile>
#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QQueue>
#include <QStringList>
#include <algorithm>

// ============================================================
//  ROM access helpers
// ============================================================

static bool validOff(const QByteArray &rom, int off, int len = 1)
{
    return off >= 0 && off + len <= rom.size();
}

static uint8_t rb(const QByteArray &rom, int off)
{
    return validOff(rom, off) ? static_cast<uint8_t>(rom.at(off)) : 0u;
}

static uint16_t rh(const QByteArray &rom, int off)
{
    return static_cast<uint16_t>(rb(rom, off)) |
           (static_cast<uint16_t>(rb(rom, off + 1)) << 8);
}

static uint32_t rw(const QByteArray &rom, int off)
{
    return static_cast<uint32_t>(rb(rom, off))
         | (static_cast<uint32_t>(rb(rom, off + 1)) << 8)
         | (static_cast<uint32_t>(rb(rom, off + 2)) << 16)
         | (static_cast<uint32_t>(rb(rom, off + 3)) << 24);
}

// Convert a raw GBA ROM pointer to a file offset; -1 if not a valid ROM pointer.
static int ptrOff(uint32_t gbaAddr)
{
    if (gbaAddr >= 0x08000000u && gbaAddr < 0x0C000000u)
        return static_cast<int>(gbaAddr - 0x08000000u);
    return -1;
}

static int readPtr(const QByteArray &rom, int off)
{
    return ptrOff(rw(rom, off));
}

// ============================================================
//  Formatting helpers
// ============================================================

static QString hB(uint8_t v)  { return "0x" + QString::number(v,  16).toUpper(); }
static QString hH(uint16_t v) { return "0x" + QString::number(v,  16).toUpper(); }
static QString hW(uint32_t v) { return "0x" + QString::number(v,  16).toUpper(); }

// GBA ROM address string from a file offset.
static QString gbaStr(int off)
{
    return "0x" + QString::number(static_cast<uint32_t>(0x08000000u + static_cast<uint32_t>(off)), 16)
                      .toUpper()
                      .rightJustified(8, '0');
}

// Zero-padded 8-char uppercase hex string for building label names.
static QString offHex(int off)
{
    return QString::number(static_cast<uint32_t>(off), 16).toUpper().rightJustified(8, '0');
}

// ============================================================
//  Named constants
// ============================================================

static const char *condName(uint8_t c)
{
    switch (c) {
    case 0: return "lt";
    case 1: return "eq";
    case 2: return "gt";
    case 3: return "le";
    case 4: return "ge";
    case 5: return "ne";
    default: return "unk";
    }
}

static const char *msgboxTypeName(uint8_t t)
{
    // Only types that map to the `msgbox` macro (loadword 0, text; callstd N).
    // Non-msgbox std functions (STD_OBTAIN_ITEM=0, STD_FIND_ITEM=1, etc.)
    // must NOT appear here — they would be incorrectly wrapped in msgbox.
    switch (t) {
    case 2:  return "MSGBOX_NPC";
    case 3:  return "MSGBOX_SIGN";
    case 4:  return "MSGBOX_DEFAULT";
    case 5:  return "MSGBOX_YESNO";
    case 6:  return "MSGBOX_AUTOCLOSE";
    case 9:  return "MSGBOX_GETPOINTS";   // Emerald only
    case 10: return "MSGBOX_POKENAV";     // Emerald only
    default: return nullptr;
    }
}

// Human-readable name for a callstd index (when NOT synthesised as a msgbox macro).
// Differs between FR and Emerald.
static const char *callstdName(uint8_t idx, bool isFR)
{
    if (isFR) {
        switch (idx) {
        case 0: return "STD_OBTAIN_ITEM";
        case 1: return "STD_FIND_ITEM";
        case 7: return "STD_OBTAIN_DECORATION";
        case 8: return "STD_PUT_ITEM_AWAY";
        case 9: return "STD_RECEIVED_ITEM";
        default: return nullptr;
        }
    } else {
        switch (idx) {
        case 0: return "STD_OBTAIN_ITEM";
        case 1: return "STD_FIND_ITEM";
        case 7: return "STD_OBTAIN_DECORATION";
        case 8: return "STD_REGISTER_MATCH_CALL";
        default: return nullptr;
        }
    }
}

// Map script tag constants (index = byte tag value).
static const char *const kTagConst[] = {
    "", // 0 = terminator
    "MAP_SCRIPT_ON_LOAD",
    "MAP_SCRIPT_ON_FRAME_TABLE",
    "MAP_SCRIPT_ON_TRANSITION",
    "MAP_SCRIPT_ON_WARP_INTO_MAP_TABLE",
    "MAP_SCRIPT_ON_RESUME",
    "MAP_SCRIPT_ON_DIVE_WARP",
    "MAP_SCRIPT_ON_RETURN_TO_FIELD",
};

// Label suffix for each tag (for non-condition-table types).
static const char *const kTagSuffix[] = {
    "",
    "OnLoad",
    "MapScriptHeader_OnFrameTable",
    "OnTransition",
    "MapScriptHeader_OnWarpIntoMap",
    "OnResume",
    "OnDiveWarp",
    "OnReturnToField",
};

// ============================================================
//  Movement action name table
// ============================================================

// Returns the assembly macro name for a movement action byte, or nullptr for unknown bytes.
// Terminator 0xFE (step_end) is handled separately in the decompiler.
static const char *movActionName(uint8_t b)
{
    static const char *const kMov[] = {
        "face_down",                         // 0x00
        "face_up",                           // 0x01
        "face_left",                         // 0x02
        "face_right",                        // 0x03
        "face_down_fast",                    // 0x04
        "face_up_fast",                      // 0x05
        "face_left_fast",                    // 0x06
        "face_right_fast",                   // 0x07
        "walk_slower_down",                  // 0x08
        "walk_slower_up",                    // 0x09
        "walk_slower_left",                  // 0x0A
        "walk_slower_right",                 // 0x0B
        "walk_slow_down",                    // 0x0C
        "walk_slow_up",                      // 0x0D
        "walk_slow_left",                    // 0x0E
        "walk_slow_right",                   // 0x0F
        "walk_down",                         // 0x10
        "walk_up",                           // 0x11
        "walk_left",                         // 0x12
        "walk_right",                        // 0x13
        "jump_2_down",                       // 0x14
        "jump_2_up",                         // 0x15
        "jump_2_left",                       // 0x16
        "jump_2_right",                      // 0x17
        "delay_1",                           // 0x18
        "delay_2",                           // 0x19
        "delay_4",                           // 0x1A
        "delay_8",                           // 0x1B
        "delay_16",                          // 0x1C
        "walk_fast_down",                    // 0x1D
        "walk_fast_up",                      // 0x1E
        "walk_fast_left",                    // 0x1F
        "walk_fast_right",                   // 0x20
        "walk_in_place_slow_down",           // 0x21
        "walk_in_place_slow_up",             // 0x22
        "walk_in_place_slow_left",           // 0x23
        "walk_in_place_slow_right",          // 0x24
        "walk_in_place_down",                // 0x25
        "walk_in_place_up",                  // 0x26
        "walk_in_place_left",                // 0x27
        "walk_in_place_right",               // 0x28
        "walk_in_place_fast_down",           // 0x29
        "walk_in_place_fast_up",             // 0x2A
        "walk_in_place_fast_left",           // 0x2B
        "walk_in_place_fast_right",          // 0x2C
        "walk_in_place_faster_down",         // 0x2D
        "walk_in_place_faster_up",           // 0x2E
        "walk_in_place_faster_left",         // 0x2F
        "walk_in_place_faster_right",        // 0x30
        "ride_water_current_down",           // 0x31
        "ride_water_current_up",             // 0x32
        "ride_water_current_left",           // 0x33
        "ride_water_current_right",          // 0x34
        "walk_faster_down",                  // 0x35
        "walk_faster_up",                    // 0x36
        "walk_faster_left",                  // 0x37
        "walk_faster_right",                 // 0x38
        "slide_down",                        // 0x39
        "slide_up",                          // 0x3A
        "slide_left",                        // 0x3B
        "slide_right",                       // 0x3C
        "player_run_down",                   // 0x3D
        "player_run_up",                     // 0x3E
        "player_run_left",                   // 0x3F
        "player_run_right",                  // 0x40
        "player_run_down_slow",              // 0x41
        "player_run_up_slow",                // 0x42
        "player_run_left_slow",              // 0x43
        "player_run_right_slow",             // 0x44
        "start_anim_in_direction",           // 0x45
        "jump_special_down",                 // 0x46
        "jump_special_up",                   // 0x47
        "jump_special_left",                 // 0x48
        "jump_special_right",                // 0x49
        "face_player",                       // 0x4A
        "face_away_player",                  // 0x4B
        "lock_facing_direction",             // 0x4C
        "unlock_facing_direction",           // 0x4D
        "jump_down",                         // 0x4E
        "jump_up",                           // 0x4F
        "jump_left",                         // 0x50
        "jump_right",                        // 0x51
        "jump_in_place_down",                // 0x52
        "jump_in_place_up",                  // 0x53
        "jump_in_place_left",                // 0x54
        "jump_in_place_right",               // 0x55
        "jump_in_place_down_up",             // 0x56
        "jump_in_place_up_down",             // 0x57
        "jump_in_place_left_right",          // 0x58
        "jump_in_place_right_left",          // 0x59
        "face_original_direction",           // 0x5A
        "nurse_joy_bow",                     // 0x5B
        "enable_jump_landing_ground_effect", // 0x5C
        "disable_jump_landing_ground_effect",// 0x5D
        "disable_anim",                      // 0x5E
        "restore_anim",                      // 0x5F
        "set_invisible",                     // 0x60
        "set_visible",                       // 0x61
        "emote_exclamation_mark",            // 0x62
        "emote_question_mark",               // 0x63
        "emote_x",                           // 0x64
        "emote_double_exclamation_mark",     // 0x65
        "emote_smile",                       // 0x66
        "reveal_trainer",                    // 0x67
        "rock_smash_break",                  // 0x68
        "cut_tree",                          // 0x69
        "set_fixed_priority",                // 0x6A
        "clear_fixed_priority",              // 0x6B
        "init_affine_anim",                  // 0x6C
        "clear_affine_anim",                 // 0x6D
        "walk_down_start_affine",            // 0x6E
        "walk_down_affine",                  // 0x6F
        "acro_wheelie_face_down",            // 0x70
        "acro_wheelie_face_up",              // 0x71
        "acro_wheelie_face_left",            // 0x72
        "acro_wheelie_face_right",           // 0x73
        "acro_pop_wheelie_down",             // 0x74
        "acro_pop_wheelie_up",               // 0x75
        "acro_pop_wheelie_left",             // 0x76
        "acro_pop_wheelie_right",            // 0x77
        "acro_end_wheelie_face_down",        // 0x78
        "acro_end_wheelie_face_up",          // 0x79
        "acro_end_wheelie_face_left",        // 0x7A
        "acro_end_wheelie_face_right",       // 0x7B
        "acro_wheelie_hop_face_down",        // 0x7C
        "acro_wheelie_hop_face_up",          // 0x7D
        "acro_wheelie_hop_face_left",        // 0x7E
        "acro_wheelie_hop_face_right",       // 0x7F
        "acro_wheelie_hop_down",             // 0x80
        "acro_wheelie_hop_up",               // 0x81
        "acro_wheelie_hop_left",             // 0x82
        "acro_wheelie_hop_right",            // 0x83
        "acro_wheelie_jump_down",            // 0x84
        "acro_wheelie_jump_up",              // 0x85
        "acro_wheelie_jump_left",            // 0x86
        "acro_wheelie_jump_right",           // 0x87
        "acro_wheelie_in_place_down",        // 0x88
        "acro_wheelie_in_place_up",          // 0x89
        "acro_wheelie_in_place_left",        // 0x8A
        "acro_wheelie_in_place_right",       // 0x8B
        "acro_pop_wheelie_move_down",        // 0x8C
        "acro_pop_wheelie_move_up",          // 0x8D
        "acro_pop_wheelie_move_left",        // 0x8E
        "acro_pop_wheelie_move_right",       // 0x8F
        "acro_wheelie_move_down",            // 0x90
        "acro_wheelie_move_up",              // 0x91
        "acro_wheelie_move_left",            // 0x92
        "acro_wheelie_move_right",           // 0x93
        "spin_down",                         // 0x94
        "spin_up",                           // 0x95
        "spin_left",                         // 0x96
        "spin_right",                        // 0x97
        "raise_hand_and_stop",               // 0x98
        "raise_hand_and_jump",               // 0x99
        "raise_hand_and_swim",               // 0x9A
        "walk_slowest_down",                 // 0x9B
        "walk_slowest_up",                   // 0x9C
        "walk_slowest_left",                 // 0x9D
        "walk_slowest_right",                // 0x9E
        "shake_head_or_walk_in_place",       // 0x9F
        "glide_down",                        // 0xA0
        "glide_up",                          // 0xA1
        "glide_left",                        // 0xA2
        "glide_right",                       // 0xA3
        "fly_up",                            // 0xA4
        "fly_down",                          // 0xA5
        "jump_special_with_effect_down",     // 0xA6
        "jump_special_with_effect_up",       // 0xA7
        "jump_special_with_effect_left",     // 0xA8
        "jump_special_with_effect_right",    // 0xA9
    };
    if (b < 0xAA) return kMov[b];
    return nullptr; // 0xAA–0xFD: unknown; 0xFE handled as step_end; 0xFF = MOVEMENT_ACTION_NONE
}

// ============================================================
//  trainerbattle pointer table
// ============================================================

struct TbInfo { int textPtrs, scriptPtrs; };

static TbInfo tbInfo(uint8_t type)
{
    switch (type) {
    case 0:  return {2, 0}; // SINGLE
    case 1:  return {2, 1}; // CONTINUE_SCRIPT_NO_MUSIC
    case 2:  return {2, 1}; // CONTINUE_SCRIPT
    case 3:  return {1, 0}; // SINGLE_NO_INTRO_TEXT
    case 4:  return {3, 0}; // DOUBLE
    case 5:  return {2, 0}; // REMATCH
    case 6:  return {3, 1}; // CONTINUE_SCRIPT_DOUBLE
    case 7:  return {3, 0}; // REMATCH_DOUBLE
    case 8:  return {3, 1}; // CONTINUE_SCRIPT_DOUBLE_NO_MUSIC
    case 9:  return {2, 0}; // PYRAMID
    case 10: return {2, 0}; // SET_TRAINER_A
    case 11: return {2, 0}; // SET_TRAINER_B
    case 12: return {2, 0}; // HILL
    default: return {2, 0};
    }
}

// ============================================================
//  Text block formatter
// ============================================================

// Decode text at `offset` and return a sequence of tab-indented .string lines.
static QString formatTextBody(const QByteArray &rom, int offset)
{
    if (!validOff(rom, offset)) return "\t.string \"$\"\n";

    int maxLen = qMin(rom.size() - offset, 4096);
    QByteArray bytes = rom.mid(offset, maxLen);

    // decodePokeText reads until 0xFF and returns the string with embedded
    // escape sequences like \n \l \p \v as two-character sequences.
    QString decoded = decodePokeText(bytes, false);

    // Split after each \n, \l, \p so each segment is on its own .string line.
    // The delimiter stays at the end of the preceding segment.
    QStringList segments;
    QString cur;
    for (int i = 0; i < decoded.size(); i++) {
        cur += decoded[i];
        if (decoded[i] == '\\' && i + 1 < decoded.size()) {
            QChar n = decoded[i + 1];
            if (n == 'n' || n == 'l' || n == 'p') {
                cur += n;
                ++i;
                segments << cur;
                cur.clear();
            }
        }
    }
    cur += '$';
    segments << cur;

    QString block;
    for (const QString &seg : segments)
        block += "\t.string \"" + seg + "\"\n";
    return block;
}

// ============================================================
//  Disassembler state
// ============================================================

struct DState
{
    const QByteArray &rom;
    QString mapName;
    bool isFR = false;

    // offset → full label string including trailing colons ("Label::" or "Label:")
    QMap<int, QString> scriptLabels;
    QMap<int, QString> textLabels;
    QMap<int, QString> movementLabels;

    QQueue<int> scriptQueue;
    QQueue<int> textQueue;
    QQueue<int> movementQueue;

    QSet<int> doneScripts;
    QSet<int> doneTexts;
    QSet<int> doneMovements;

    // offset → body text (without the leading label line)
    QMap<int, QString> scriptBodies;
    QMap<int, QString> textBodies;
    QMap<int, QString> movementBodies;

    // The MapScripts:: header block, output first regardless of offset ordering.
    QString mapScriptsBlock;
};

// Strip trailing colons to get the bare label name for use as an argument.
static QString labelName(const QString &labelWithColons)
{
    QString s = labelWithColons;
    while (!s.isEmpty() && s.back() == ':') s.chop(1);
    return s;
}

// Get or create a Script label; queues the offset for disassembly if new.
static QString getOrMakeScriptRef(DState &s, int off)
{
    if (!s.scriptLabels.contains(off)) {
        s.scriptLabels[off] = s.mapName + "_Script_" + offHex(off) + "::";
        if (!s.doneScripts.contains(off))
            s.scriptQueue.enqueue(off);
    }
    return labelName(s.scriptLabels[off]);
}

// Get or create a Text label; queues the offset for text decoding if new.
static QString getOrMakeTextRef(DState &s, int off)
{
    if (!s.textLabels.contains(off)) {
        s.textLabels[off] = s.mapName + "_Text_" + offHex(off) + ":";
        if (!s.doneTexts.contains(off))
            s.textQueue.enqueue(off);
    }
    return labelName(s.textLabels[off]);
}

// Get or create a Movement label; queues the offset for movement decoding if new.
static QString getOrMakeMovementRef(DState &s, int off)
{
    if (!s.movementLabels.contains(off)) {
        s.movementLabels[off] = s.mapName + "_Movement_" + offHex(off) + "::";
        if (!s.doneMovements.contains(off))
            s.movementQueue.enqueue(off);
    }
    return labelName(s.movementLabels[off]);
}

// Decompile a movement script at the given ROM offset.
static QString decompileMovement(const QByteArray &rom, int startOff)
{
    if (!validOff(rom, startOff)) return "\tstep_end\n";

    QString block;
    int pc = startOff;

    for (int guard = 0; guard < 512; guard++) {
        if (!validOff(rom, pc)) break;
        uint8_t b = rb(rom, pc++);

        if (b == 0xFE) { block += "\tstep_end\n"; break; }
        if (b == 0xFF) { block += "\t.byte 0xFF\n"; break; }

        const char *name = movActionName(b);
        if (name)
            block += "\t" + QString(name) + "\n";
        else
            block += QString("\t.byte %1 @ unknown movement\n").arg(hB(b));
    }
    return block;
}

// ============================================================
//  Script disassembler
// ============================================================

static QString disassembleScript(DState &s, int startOff)
{
    if (!validOff(s.rom, startOff))
        return "\t.byte 0xFF @ invalid offset\n";

    struct OpInfo { const char *name; const char *fmt; bool term; };

    // fmt chars: b=u8  h=u16  w=raw u32  s=script ptr  t=text ptr  m=movement/data ptr
    // Entries for the special-cased opcodes are placeholders (never reached).
    static const OpInfo kOps[256] = {
        {"nop",                       "",        false}, // 0x00
        {"nop1",                      "",        false}, // 0x01
        {"end",                       "",        true }, // 0x02
        {"return",                    "",        true }, // 0x03
        {"call",                      "s",       false}, // 0x04
        {"goto",                      "s",       true }, // 0x05
        {"goto_if",                   "bs",      false}, // 0x06
        {"call_if",                   "bs",      false}, // 0x07
        {"gotostd",                   "b",       false}, // 0x08
        {"callstd",                   "b",       false}, // 0x09
        {"gotostd_if",                "bb",      false}, // 0x0A
        {"callstd_if",                "bb",      false}, // 0x0B
        {"returnram",                 "",        true }, // 0x0C
        {"endram",                    "",        true }, // 0x0D
        {"setmysteryeventstatus",     "b",       false}, // 0x0E
        {"loadword",                  "bw",      false}, // 0x0F  (also macro entry)
        {"loadbyte",                  "bb",      false}, // 0x10
        {"setptr",                    "bw",      false}, // 0x11
        {"loadbytefromptr",           "bw",      false}, // 0x12
        {"setptrbyte",                "bw",      false}, // 0x13
        {"copylocal",                 "bb",      false}, // 0x14
        {"copybyte",                  "ww",      false}, // 0x15
        {"setvar",                    "hh",      false}, // 0x16
        {"addvar",                    "hh",      false}, // 0x17
        {"subvar",                    "hh",      false}, // 0x18
        {"copyvar",                   "hh",      false}, // 0x19
        {"setorcopyvar",              "hh",      false}, // 0x1A
        {"compare_local_to_local",    "bb",      false}, // 0x1B
        {"compare_local_to_value",    "bb",      false}, // 0x1C
        {"compare_local_to_ptr",      "bw",      false}, // 0x1D
        {"compare_ptr_to_local",      "wb",      false}, // 0x1E
        {"compare_ptr_to_value",      "wb",      false}, // 0x1F
        {"compare_ptr_to_ptr",        "ww",      false}, // 0x20
        {"compare_var_to_value",      "hh",      false}, // 0x21  (also macro entry)
        {"compare_var_to_var",        "hh",      false}, // 0x22  (also macro entry)
        {"callnative",                "w",       false}, // 0x23
        {"gotonative",                "w",       true }, // 0x24
        {"special",                   "h",       false}, // 0x25
        {"specialvar",                "hh",      false}, // 0x26
        {"waitstate",                 "",        false}, // 0x27
        {"delay",                     "h",       false}, // 0x28
        {"setflag",                   "h",       false}, // 0x29
        {"clearflag",                 "h",       false}, // 0x2A
        {"checkflag",                 "h",       false}, // 0x2B  (also macro entry)
        {"initclock",                 "hh",      false}, // 0x2C
        {"dotimebasedevents",         "",        false}, // 0x2D
        {"gettime",                   "",        false}, // 0x2E
        {"playse",                    "h",       false}, // 0x2F
        {"waitse",                    "",        false}, // 0x30
        {"playfanfare",               "h",       false}, // 0x31
        {"waitfanfare",               "",        false}, // 0x32
        {"playbgm",                   "hb",      false}, // 0x33
        {"savebgm",                   "h",       false}, // 0x34
        {"fadedefaultbgm",            "",        false}, // 0x35
        {"fadenewbgm",                "h",       false}, // 0x36
        {"fadeoutbgm",                "b",       false}, // 0x37
        {"fadeinbgm",                 "b",       false}, // 0x38
        {"warp",                      "bbbhh",   true }, // 0x39
        {"warpsilent",                "bbbhh",   true }, // 0x3A
        {"warpdoor",                  "bbbhh",   true }, // 0x3B
        {"warphole",                  "bb",      true }, // 0x3C
        {"warpteleport",              "bbbhh",   true }, // 0x3D
        {"setwarp",                   "bbbhh",   false}, // 0x3E
        {"setdynamicwarp",            "bbbhh",   false}, // 0x3F
        {"setdivewarp",               "bbbhh",   false}, // 0x40
        {"setholewarp",               "bbbhh",   false}, // 0x41
        {"getplayerxy",               "hh",      false}, // 0x42
        {"getpartysize",              "",        false}, // 0x43
        {"additem",                   "hh",      false}, // 0x44
        {"removeitem",                "hh",      false}, // 0x45
        {"checkitemspace",            "hh",      false}, // 0x46
        {"checkitem",                 "hh",      false}, // 0x47
        {"checkitemtype",             "h",       false}, // 0x48
        {"addpcitem",                 "hh",      false}, // 0x49
        {"checkpcitem",               "hh",      false}, // 0x4A
        {"adddecoration",             "h",       false}, // 0x4B
        {"removedecoration",          "h",       false}, // 0x4C
        {"checkdecor",                "h",       false}, // 0x4D
        {"checkdecorspace",           "h",       false}, // 0x4E
        {"applymovement",             "hm",      false}, // 0x4F
        {"applymovementat",           "hmbb",    false}, // 0x50
        {"waitmovement",              "h",       false}, // 0x51
        {"waitmovementat",            "hbb",     false}, // 0x52
        {"removeobject",              "h",       false}, // 0x53
        {"removeobjectat",            "hbb",     false}, // 0x54
        {"addobject",                 "h",       false}, // 0x55
        {"addobjectat",               "hbb",     false}, // 0x56
        {"setobjectxy",               "hhh",     false}, // 0x57
        {"showobjectat",              "hbb",     false}, // 0x58
        {"hideobjectat",              "hbb",     false}, // 0x59
        {"faceplayer",                "",        false}, // 0x5A
        {"turnobject",                "hb",      false}, // 0x5B
        {"trainerbattle",             "",        false}, // 0x5C  (special handler)
        {"dotrainerbattle",           "",        false}, // 0x5D
        {"gotopostbattlescript",      "",        true }, // 0x5E
        {"gotobeatenscript",          "",        true }, // 0x5F
        {"checktrainerflag",          "h",       false}, // 0x60  (also macro entry)
        {"settrainerflag",            "h",       false}, // 0x61
        {"cleartrainerflag",          "h",       false}, // 0x62
        {"setobjectxyperm",           "hhh",     false}, // 0x63
        {"copyobjectxytoperm",        "h",       false}, // 0x64
        {"setobjectmovementtype",     "hb",      false}, // 0x65
        {"waitmessage",               "",        false}, // 0x66
        {"message",                   "t",       false}, // 0x67
        {"closemessage",              "",        false}, // 0x68
        {"lockall",                   "",        false}, // 0x69
        {"lock",                      "",        false}, // 0x6A
        {"releaseall",                "",        false}, // 0x6B
        {"release",                   "",        false}, // 0x6C
        {"waitbuttonpress",           "",        false}, // 0x6D
        {"yesnobox",                  "bb",      false}, // 0x6E
        {"multichoice",               "bbbb",    false}, // 0x6F
        {"multichoicedefault",        "bbbbb",   false}, // 0x70
        {"multichoicegrid",           "bbbbb",   false}, // 0x71
        {"drawbox",                   "",        false}, // 0x72  (nop in Emerald)
        {"erasebox",                  "bbbb",    false}, // 0x73
        {"drawboxtext",               "bbbb",    false}, // 0x74
        {"showmonpic",                "hbb",     false}, // 0x75
        {"hidemonpic",                "",        false}, // 0x76
        {"showcontestpainting",       "b",       false}, // 0x77
        {"braillemessage",            "t",       false}, // 0x78
        {"givemon",                   "hbhwwb",  false}, // 0x79
        {"giveegg",                   "h",       false}, // 0x7A
        {"setmonmove",                "bbh",     false}, // 0x7B
        {"checkpartymove",            "h",       false}, // 0x7C
        {"bufferspeciesname",         "bh",      false}, // 0x7D
        {"bufferleadmonspeciesname",  "b",       false}, // 0x7E
        {"bufferpartymonnick",        "bh",      false}, // 0x7F
        {"bufferitemname",            "bh",      false}, // 0x80
        {"bufferdecorationname",      "bh",      false}, // 0x81
        {"buffermovename",            "bh",      false}, // 0x82
        {"buffernumberstring",        "bh",      false}, // 0x83
        {"bufferstdstring",           "bh",      false}, // 0x84
        {"bufferstring",              "bt",      false}, // 0x85
        {"pokemart",                  "m",       false}, // 0x86
        {"pokemartdecoration",        "m",       false}, // 0x87
        {"pokemartdecoration2",       "m",       false}, // 0x88
        {"playslotmachine",           "h",       false}, // 0x89
        {"setberrytree",              "bbb",     false}, // 0x8A
        {"choosecontestmon",          "",        false}, // 0x8B
        {"startcontest",              "",        false}, // 0x8C
        {"showcontestresults",        "",        false}, // 0x8D
        {"contestlinktransfer",       "",        false}, // 0x8E
        {"random",                    "h",       false}, // 0x8F
        {"addmoney",                  "wb",      false}, // 0x90
        {"removemoney",               "wb",      false}, // 0x91
        {"checkmoney",                "wb",      false}, // 0x92
        {"showmoneybox",              "bbb",     false}, // 0x93
        {"hidemoneybox",              "",        false}, // 0x94
        {"updatemoneybox",            "bbb",     false}, // 0x95
        {"getpokenewsactive",         "h",       false}, // 0x96
        {"fadescreen",                "b",       false}, // 0x97
        {"fadescreenspeed",           "bb",      false}, // 0x98
        {"setflashlevel",             "h",       false}, // 0x99
        {"animateflash",              "b",       false}, // 0x9A
        {"messageautoscroll",         "t",       false}, // 0x9B
        {"dofieldeffect",             "h",       false}, // 0x9C
        {"setfieldeffectargument",    "bh",      false}, // 0x9D
        {"waitfieldeffect",           "h",       false}, // 0x9E
        {"setrespawn",                "h",       false}, // 0x9F
        {"checkplayergender",         "",        false}, // 0xA0
        {"playmoncry",                "hh",      false}, // 0xA1
        {"setmetatile",               "hhhh",    false}, // 0xA2
        {"resetweather",              "",        false}, // 0xA3
        {"setweather",                "h",       false}, // 0xA4
        {"doweather",                 "",        false}, // 0xA5
        {"setstepcallback",           "b",       false}, // 0xA6
        {"setmaplayoutindex",         "h",       false}, // 0xA7
        {"setobjectsubpriority",      "hbbb",    false}, // 0xA8
        {"resetobjectsubpriority",    "hbb",     false}, // 0xA9
        {"createvobject",             "bbhhbb",  false}, // 0xAA
        {"turnvobject",               "bb",      false}, // 0xAB
        {"opendoor",                  "hh",      false}, // 0xAC
        {"closedoor",                 "hh",      false}, // 0xAD
        {"waitdooranim",              "",        false}, // 0xAE
        {"setdooropen",               "hh",      false}, // 0xAF
        {"setdoorclosed",             "hh",      false}, // 0xB0
        {"addelevmenuitem",           "bhhh",    false}, // 0xB1
        {"showelevmenu",              "",        false}, // 0xB2
        {"checkcoins",                "h",       false}, // 0xB3
        {"addcoins",                  "h",       false}, // 0xB4
        {"removecoins",               "h",       false}, // 0xB5
        {"setwildbattle",             "hbh",     false}, // 0xB6
        {"dowildbattle",              "",        false}, // 0xB7
        {"setvaddress",               "w",       false}, // 0xB8
        {"vgoto",                     "w",       true }, // 0xB9
        {"vcall",                     "w",       false}, // 0xBA
        {"vgoto_if",                  "bw",      false}, // 0xBB
        {"vcall_if",                  "bw",      false}, // 0xBC
        {"vmessage",                  "w",       false}, // 0xBD
        {"vbuffermessage",            "w",       false}, // 0xBE
        {"vbufferstring",             "bw",      false}, // 0xBF
        {"showcoinsbox",              "bb",      false}, // 0xC0
        {"hidecoinsbox",              "bb",      false}, // 0xC1
        {"updatecoinsbox",            "bb",      false}, // 0xC2
        {"incrementgamestat",         "b",       false}, // 0xC3
        {"setescapewarp",             "bbbhh",   false}, // 0xC4
        {"waitmoncry",                "",        false}, // 0xC5
        {"bufferboxname",             "bh",      false}, // 0xC6
        {"textcolor",                 "",        false}, // 0xC7  nop1 in Emerald
        {"loadhelp",                  "",        false}, // 0xC8  nop1
        {"unloadhelp",                "",        false}, // 0xC9  nop1
        {"signmsg",                   "",        false}, // 0xCA  nop1
        {"normalmsg",                 "",        false}, // 0xCB  nop1
        {"comparehiddenvar",          "",        false}, // 0xCC  nop1 in Emerald
        {"setmodernfatefulencounter", "h",       false}, // 0xCD
        {"checkmodernfatefulencounter","h",      false}, // 0xCE
        {"trywondercardscript",       "",        false}, // 0xCF
        {"setworldmapflag",           "",        false}, // 0xD0  nop1 in Emerald
        {"warpspinenter",             "bbbhh",   true }, // 0xD1
        {"setmonmetlocation",         "hb",      false}, // 0xD2
        {"moverotatingtileobjects",   "h",       false}, // 0xD3
        {"turnrotatingtileobjects",   "",        false}, // 0xD4
        {"initrotatingtilepuzzle",    "h",       false}, // 0xD5
        {"freerotatingtilepuzzle",    "",        false}, // 0xD6
        {"warpmossdeepgym",           "bbbhh",   true }, // 0xD7
        {"selectapproachingtrainer",  "",        false}, // 0xD8
        {"lockfortrainer",            "",        false}, // 0xD9
        {"closebraillemessage",       "",        false}, // 0xDA
        {"messageinstant",            "t",       false}, // 0xDB
        {"fadescreenswapbuffers",     "b",       false}, // 0xDC
        {"buffertrainerclassname",    "bh",      false}, // 0xDD
        {"buffertrainername",         "bh",      false}, // 0xDE
        {"pokenavcall",               "t",       false}, // 0xDF
        {"warpwhitefade",             "bbbhh",   true }, // 0xE0
        {"buffercontestname",         "bh",      false}, // 0xE1
        {"bufferitemnameplural",      "bhh",     false}, // 0xE2
    };
    // 0xE3–0xFF: zero-initialized (name==nullptr), treated as unknown.

    QString block;
    int pc = startOff;

    for (int guard = 0; guard < 2000; guard++) {
        if (!validOff(s.rom, pc)) break;
        uint8_t op = rb(s.rom, pc++);

        // --------------------------------------------------------
        // loadword (0x0F): detect msgbox macro
        // --------------------------------------------------------
        if (op == 0x0F) {
            uint8_t dstIdx = rb(s.rom, pc++);
            uint32_t val   = rw(s.rom, pc); pc += 4;

            // msgbox pattern: loadword 0, PTR  +  callstd N
            if (dstIdx == 0 && rb(s.rom, pc) == 0x09) {
                uint8_t csa = rb(s.rom, pc + 1);
                const char *mbn = msgboxTypeName(csa);
                if (mbn) {
                    pc += 2; // consume callstd opcode + arg
                    int toff = ptrOff(val);
                    QString txtRef = (toff >= 0) ? getOrMakeTextRef(s, toff) : hW(val);
                    block += QString("\tmsgbox %1, %2\n").arg(txtRef, mbn);
                    continue;
                }
            }
            block += QString("\tloadword %1, %2\n").arg(hB(dstIdx), hW(val));
            continue;
        }

        // --------------------------------------------------------
        // checkflag (0x2B): detect goto_if_set / goto_if_unset / call_if_set / call_if_unset
        // --------------------------------------------------------
        if (op == 0x2B) {
            uint16_t flag = rh(s.rom, pc); pc += 2;
            uint8_t nextOp = rb(s.rom, pc);
            if (nextOp == 0x06 || nextOp == 0x07) {
                uint8_t cond  = rb(s.rom, pc + 1);
                int destOff   = readPtr(s.rom, pc + 2);
                if (destOff >= 0 && (cond == 0 || cond == 1)) {
                    pc += 6; // skip goto_if / call_if + cond + 4-byte ptr
                    QString dest = getOrMakeScriptRef(s, destOff);
                    bool isGoto  = (nextOp == 0x06);
                    if (cond == 1)
                        block += QString("\t%1_if_set %2, %3\n").arg(isGoto ? "goto" : "call", hH(flag), dest);
                    else
                        block += QString("\t%1_if_unset %2, %3\n").arg(isGoto ? "goto" : "call", hH(flag), dest);
                    continue;
                }
            }
            block += QString("\tcheckflag %1\n").arg(hH(flag));
            continue;
        }

        // --------------------------------------------------------
        // compare_var_to_value (0x21): detect conditional branch macros
        // --------------------------------------------------------
        if (op == 0x21) {
            uint16_t var = rh(s.rom, pc); pc += 2;
            uint16_t val = rh(s.rom, pc); pc += 2;
            uint8_t nextOp = rb(s.rom, pc);
            if (nextOp == 0x06 || nextOp == 0x07) {
                uint8_t cond = rb(s.rom, pc + 1);
                int destOff  = readPtr(s.rom, pc + 2);
                if (destOff >= 0) {
                    pc += 6;
                    QString dest  = getOrMakeScriptRef(s, destOff);
                    QString macro = QString(nextOp == 0x06 ? "goto_if_" : "call_if_") + condName(cond);
                    block += QString("\t%1 %2, %3, %4\n").arg(macro, hH(var), hH(val), dest);
                    continue;
                }
            }
            block += QString("\tcompare_var_to_value %1, %2\n").arg(hH(var), hH(val));
            continue;
        }

        // --------------------------------------------------------
        // compare_var_to_var (0x22): detect conditional branch macros
        // --------------------------------------------------------
        if (op == 0x22) {
            uint16_t var1 = rh(s.rom, pc); pc += 2;
            uint16_t var2 = rh(s.rom, pc); pc += 2;
            uint8_t nextOp = rb(s.rom, pc);
            if (nextOp == 0x06 || nextOp == 0x07) {
                uint8_t cond = rb(s.rom, pc + 1);
                int destOff  = readPtr(s.rom, pc + 2);
                if (destOff >= 0) {
                    pc += 6;
                    QString dest  = getOrMakeScriptRef(s, destOff);
                    QString macro = QString(nextOp == 0x06 ? "goto_if_" : "call_if_") + condName(cond);
                    block += QString("\t%1 %2, %3, %4\n").arg(macro, hH(var1), hH(var2), dest);
                    continue;
                }
            }
            block += QString("\tcompare_var_to_var %1, %2\n").arg(hH(var1), hH(var2));
            continue;
        }

        // --------------------------------------------------------
        // checktrainerflag (0x60): detect goto_if_defeated / goto_if_not_defeated
        // --------------------------------------------------------
        if (op == 0x60) {
            uint16_t trainer = rh(s.rom, pc); pc += 2;
            uint8_t nextOp = rb(s.rom, pc);
            if (nextOp == 0x06 || nextOp == 0x07) {
                uint8_t cond = rb(s.rom, pc + 1);
                int destOff  = readPtr(s.rom, pc + 2);
                if (destOff >= 0 && (cond == 0 || cond == 1)) {
                    pc += 6;
                    QString dest  = getOrMakeScriptRef(s, destOff);
                    bool isGoto   = (nextOp == 0x06);
                    if (cond == 1)
                        block += QString("\t%1_if_defeated %2, %3\n").arg(isGoto ? "goto" : "call", hH(trainer), dest);
                    else
                        block += QString("\t%1_if_not_defeated %2, %3\n").arg(isGoto ? "goto" : "call", hH(trainer), dest);
                    continue;
                }
            }
            block += QString("\tchecktrainerflag %1\n").arg(hH(trainer));
            continue;
        }

        // --------------------------------------------------------
        // applymovement (0x4F): decode movement pointer
        // --------------------------------------------------------
        if (op == 0x4F) {
            uint16_t npcId = rh(s.rom, pc); pc += 2;
            int movOff = readPtr(s.rom, pc); pc += 4;
            QString movRef = (movOff >= 0) ? getOrMakeMovementRef(s, movOff) : hW(rw(s.rom, pc - 4));
            block += QString("\tapplymovement %1, %2\n").arg(hH(npcId), movRef);
            continue;
        }

        // --------------------------------------------------------
        // applymovementat (0x50): decode movement pointer
        // --------------------------------------------------------
        if (op == 0x50) {
            uint16_t npcId  = rh(s.rom, pc); pc += 2;
            int movOff      = readPtr(s.rom, pc); pc += 4;
            uint8_t mapNum  = rb(s.rom, pc++);
            uint8_t bankNum = rb(s.rom, pc++);
            QString movRef  = (movOff >= 0) ? getOrMakeMovementRef(s, movOff) : hW(rw(s.rom, pc - 6));
            block += QString("\tapplymovementat %1, %2, %3, %4\n")
                         .arg(hH(npcId), movRef, hB(mapNum), hB(bankNum));
            continue;
        }

        // --------------------------------------------------------
        // trainerbattle (0x5C): variable-length special handler
        // --------------------------------------------------------
        if (op == 0x5C) {
            uint8_t  type      = rb(s.rom, pc++);
            uint16_t trainerId = rh(s.rom, pc); pc += 2;
            uint16_t localId   = rh(s.rom, pc); pc += 2;
            TbInfo   info      = tbInfo(type);
            QStringList args;
            args << hB(type) << hH(trainerId) << hH(localId);
            for (int i = 0; i < info.textPtrs; ++i) {
                int toff = readPtr(s.rom, pc); pc += 4;
                args << (toff >= 0 ? getOrMakeTextRef(s, toff) : gbaStr(toff));
            }
            for (int i = 0; i < info.scriptPtrs; ++i) {
                int soff = readPtr(s.rom, pc); pc += 4;
                args << (soff >= 0 ? getOrMakeScriptRef(s, soff) : gbaStr(soff));
            }
            block += "\ttrainerbattle " + args.join(", ") + "\n";
            continue;
        }

        // --------------------------------------------------------
        // FR-specific opcode overrides
        // FireRed has different parameter sizes for a few opcodes
        // that Emerald treats as nop1 or uses differently.
        // --------------------------------------------------------
        if (s.isFR) {
            bool handled = true;
            switch (op) {
            case 0xC7: { // textcolor: reads 1 byte in FR (nop1 in Emerald)
                uint8_t color = rb(s.rom, pc++);
                block += QString("\ttextcolor %1\n").arg(hB(color));
                break;
            }
            case 0xC8: { // loadhelp: reads a word ptr in FR (nop1 in Emerald)
                uint32_t raw = rw(s.rom, pc); pc += 4;
                int toff = ptrOff(raw);
                QString ref = (toff >= 0) ? getOrMakeTextRef(s, toff) : hW(raw);
                block += QString("\tloadhelp %1\n").arg(ref);
                break;
            }
            case 0xCC: { // comparestat in FR; comparehiddenvar (nop1) in Emerald
                uint8_t statIdx = rb(s.rom, pc++);
                uint32_t val    = rw(s.rom, pc); pc += 4;
                block += QString("\tcomparestat %1, %2\n").arg(hB(statIdx), hW(val));
                break;
            }
            case 0xD0: { // setworldmapflag: reads halfword in FR (nop1 in Emerald)
                uint16_t flag = rh(s.rom, pc); pc += 2;
                block += QString("\tsetworldmapflag %1\n").arg(hH(flag));
                break;
            }
            case 0xD3: { // getbraillestringwidth: reads word ptr in FR
                uint32_t raw = rw(s.rom, pc); pc += 4;
                block += QString("\tgetbraillestringwidth %1\n").arg(hW(raw));
                break;
            }
            case 0xD4: { // bufferitemnameplural in FR: reads b h h
                uint8_t  b1 = rb(s.rom, pc++);
                uint16_t h1 = rh(s.rom, pc); pc += 2;
                uint16_t h2 = rh(s.rom, pc); pc += 2;
                block += QString("\tbufferitemnameplural %1, %2, %3\n").arg(hB(b1), hH(h1), hH(h2));
                break;
            }
            default:
                handled = false;
                break;
            }
            if (handled) continue;
        }

        // --------------------------------------------------------
        // goto (0x05): terminator, but may be followed by a dead `end`/`return`
        // byte which the original sources consistently include.
        // --------------------------------------------------------
        if (op == 0x05) {
            int soff = readPtr(s.rom, pc); pc += 4;
            QString target = (soff >= 0) ? getOrMakeScriptRef(s, soff) : hW(rw(s.rom, pc - 4));
            block += "\tgoto " + target + "\n";
            if (validOff(s.rom, pc)) {
                uint8_t tail = rb(s.rom, pc);
                if (tail == 0x02) { block += "\tend\n"; }
                else if (tail == 0x03) { block += "\treturn\n"; }
            }
            break;
        }

        // --------------------------------------------------------
        // callstd (0x09): use named constant when available
        // --------------------------------------------------------
        if (op == 0x09) {
            uint8_t idx = rb(s.rom, pc++);
            const char *stdName = callstdName(idx, s.isFR);
            if (stdName)
                block += QString("\tcallstd %1\n").arg(stdName);
            else
                block += QString("\tcallstd %1\n").arg(hB(idx));
            continue;
        }

        // --------------------------------------------------------
        // Generic opcode table
        // --------------------------------------------------------
        uint8_t maxValidOp = s.isFR ? 0xD4 : 0xE2;
        if (op > maxValidOp || kOps[op].name == nullptr) {
            block += QString("\t.byte %1 @ unknown opcode\n").arg(hB(op));
            break; // can't continue safely
        }

        const OpInfo &info = kOps[op];
        QStringList args;

        for (const char *fp = info.fmt; *fp; ++fp) {
            switch (*fp) {
            case 'b':
                args << hB(rb(s.rom, pc));
                pc += 1;
                break;
            case 'h':
                args << hH(rh(s.rom, pc));
                pc += 2;
                break;
            case 'w':
                args << hW(rw(s.rom, pc));
                pc += 4;
                break;
            case 's': {
                int soff = readPtr(s.rom, pc); pc += 4;
                args << (soff >= 0 ? getOrMakeScriptRef(s, soff) : gbaStr(soff));
                break;
            }
            case 't': {
                int toff = readPtr(s.rom, pc); pc += 4;
                args << (toff >= 0 ? getOrMakeTextRef(s, toff) : gbaStr(toff));
                break;
            }
            case 'm':
                // Movement / item list pointer: emit as GBA address, not decoded here.
                args << hW(rw(s.rom, pc));
                pc += 4;
                break;
            }
        }

        block += args.isEmpty()
                ? QString("\t%1\n").arg(info.name)
                : QString("\t%1 %2\n").arg(info.name, args.join(", "));

        if (info.term) break;
    }

    return block;
}

// ============================================================
//  Map scripts table decoder
// ============================================================

// Decodes the (tag, ptr)* 0x00 table at `tableOff` and:
//  - Appends map_script directives to the returned block
//  - Inserts condition-table sub-blocks directly into s.scriptBodies / scriptLabels
//  - Queues non-table scripts for disassembly
static QString decodeMapScriptsTable(DState &s, int tableOff)
{
    if (!validOff(s.rom, tableOff)) return "\t.byte 0\n";

    QString block;
    int pc = tableOff;

    while (validOff(s.rom, pc)) {
        uint8_t tag = rb(s.rom, pc++);
        if (tag == 0) { block += "\t.byte 0\n"; break; }
        if (tag > 7) {
            block += QString("\t.byte %1 @ unknown map script tag\n").arg(hB(tag));
            break;
        }
        int scriptPtr = readPtr(s.rom, pc); pc += 4;
        if (scriptPtr < 0) {
            block += QString("\t@ invalid ptr for %1\n").arg(kTagConst[tag]);
            continue;
        }

        const char *tagConst = kTagConst[tag];
        const char *suffix   = kTagSuffix[tag];

        if (tag == 2 || tag == 4) {
            // Pointer is to a var/condition table, not a runnable script.
            QString headerLabel = s.mapName + "_" + suffix;
            block += QString("\tmap_script %1, %2\n").arg(tagConst, headerLabel);

            if (!s.scriptBodies.contains(scriptPtr)) {
                // Decode the condition table.
                QString tableBody;
                int tpc = scriptPtr;
                while (validOff(s.rom, tpc, 2)) {
                    uint16_t var1 = rh(s.rom, tpc);
                    if (var1 == 0) { tableBody += "\t.2byte 0\n"; break; }
                    uint16_t var2 = rh(s.rom, tpc + 2);
                    int entryPtr  = readPtr(s.rom, tpc + 4);
                    tpc += 8;
                    QString dest = (entryPtr >= 0)
                                    ? getOrMakeScriptRef(s, entryPtr)
                                    : gbaStr(entryPtr);
                    tableBody += QString("\tmap_script_2 %1, %2, %3\n").arg(hH(var1), hH(var2), dest);
                }
                s.scriptBodies[scriptPtr] = tableBody;
                s.scriptLabels[scriptPtr] = headerLabel + ":"; // local label
                s.doneScripts.insert(scriptPtr); // prevent re-processing as a code script
            }
        } else {
            // Direct runnable script.
            QString scriptLabel = s.mapName + "_" + suffix;
            block += QString("\tmap_script %1, %2\n").arg(tagConst, scriptLabel);
            if (!s.scriptLabels.contains(scriptPtr))
                s.scriptLabels[scriptPtr] = scriptLabel + "::"; // exported
            if (!s.doneScripts.contains(scriptPtr))
                s.scriptQueue.enqueue(scriptPtr);
        }
    }
    return block;
}

// ============================================================
//  Public API
// ============================================================

QString decompileMapScripts(
    const QString &romPath,
    const QString &mapName,
    int mapScriptsOff,
    const QList<int> &eventScripts,
    bool isFR)
{
    QFile f(romPath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray rom = f.readAll();
    f.close();
    if (rom.isEmpty()) return {};

    DState s{rom, mapName};
    s.isFR = isFR;

    // ── Step 1: Map scripts table ──────────────────────────────
    QString mapScriptsLabel = mapName + "_MapScripts::";
    s.mapScriptsBlock = mapScriptsLabel + "\n";

    if (validOff(rom, mapScriptsOff)) {
        s.scriptLabels[mapScriptsOff] = mapScriptsLabel;
        s.doneScripts.insert(mapScriptsOff);
        s.mapScriptsBlock += decodeMapScriptsTable(s, mapScriptsOff);
    } else {
        s.mapScriptsBlock += "\t.byte 0\n";
    }
    s.mapScriptsBlock += "\n";

    // ── Step 2: Event script pointers ─────────────────────────
    for (int off : eventScripts) {
        if (!validOff(rom, off)) continue;
        if (!s.scriptLabels.contains(off))
            s.scriptLabels[off] = mapName + "_EventScript_" + offHex(off) + "::";
        if (!s.doneScripts.contains(off))
            s.scriptQueue.enqueue(off);
    }

    // ── Step 3: Disassembly loop ───────────────────────────────
    while (!s.scriptQueue.isEmpty()) {
        int off = s.scriptQueue.dequeue();
        if (s.doneScripts.contains(off)) continue;
        s.doneScripts.insert(off);
        if (!validOff(rom, off)) continue;
        if (!s.scriptBodies.contains(off))
            s.scriptBodies[off] = disassembleScript(s, off);
    }

    // ── Step 4: Text decoding loop ────────────────────────────
    while (!s.textQueue.isEmpty()) {
        int off = s.textQueue.dequeue();
        if (s.doneTexts.contains(off)) continue;
        s.doneTexts.insert(off);
        if (!validOff(rom, off)) continue;
        s.textBodies[off] = formatTextBody(rom, off);
    }

    // ── Step 5: Movement decompilation loop ───────────────────
    while (!s.movementQueue.isEmpty()) {
        int off = s.movementQueue.dequeue();
        if (s.doneMovements.contains(off)) continue;
        s.doneMovements.insert(off);
        if (!validOff(rom, off)) continue;
        s.movementBodies[off] = decompileMovement(rom, off);
    }

    // ── Step 6: Assemble output ───────────────────────────────
    QString out = s.mapScriptsBlock;

    // Scripts sorted by ROM offset (condition table headers and code scripts interleaved naturally).
    QList<int> scriptOffsets = s.scriptBodies.keys();
    std::sort(scriptOffsets.begin(), scriptOffsets.end());
    for (int off : scriptOffsets) {
        QString fullLabel = s.scriptLabels.value(off, mapName + "_Script_" + offHex(off) + "::");
        out += fullLabel + "\n";
        out += s.scriptBodies[off];
        out += "\n";
    }

    // Movement blocks sorted by ROM offset (between scripts and text, matching decompiler convention).
    QList<int> movementOffsets = s.movementBodies.keys();
    std::sort(movementOffsets.begin(), movementOffsets.end());
    for (int off : movementOffsets) {
        QString fullLabel = s.movementLabels.value(off, mapName + "_Movement_" + offHex(off) + "::");
        out += fullLabel + "\n";
        out += s.movementBodies[off];
        out += "\n";
    }

    // Text blocks sorted by ROM offset.
    QList<int> textOffsets = s.textBodies.keys();
    std::sort(textOffsets.begin(), textOffsets.end());
    for (int off : textOffsets) {
        QString fullLabel = s.textLabels.value(off, mapName + "_Text_" + offHex(off) + ":");
        out += fullLabel + "\n";
        out += s.textBodies[off];
        out += "\n";
    }

    return out;
}
