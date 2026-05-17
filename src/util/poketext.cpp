#include "poketext.h"

// Processes each byte, maps it to the corresponding string via a lookup table,
// stops at the GBA string terminator (0xFF).

QString decodePokeText(const QByteArray& bytes, bool japanese)
{
    QString result;
    bool nextIsHex = false; // after 0xFC/0xFD (japanese mode only), next char is raw hex

    for (int idx = 0; idx < bytes.size(); idx++) {
        unsigned char x = static_cast<unsigned char>(bytes[idx]);

        if (x == 0xFF) break; // string terminator

        if (nextIsHex) {
            result += QString("\\h%1").arg(x, 2, 16, QChar('0')).toUpper();
            nextIsHex = false;
            continue;
        }

        QString y;

        if (japanese) {
            switch (x) {
            case 0x00: y = " "; break;
            case 0x01: y = "\u3042"; break; // あ
            case 0x02: y = "\u3044"; break; // い
            case 0x03: y = "\u3046"; break; // う
            case 0x04: y = "\u3048"; break; // え
            case 0x05: y = "\u304A"; break; // お
            case 0x06: y = "\u304B"; break; // か
            case 0x07: y = "\u304D"; break; // き
            case 0x08: y = "\u304F"; break; // く
            case 0x09: y = "\u3051"; break; // け
            case 0x0A: y = "\u3053"; break; // こ
            case 0x0B: y = "\u3055"; break; // さ
            case 0x0C: y = "\u3057"; break; // し
            case 0x0D: y = "\u3059"; break; // す
            case 0x0E: y = "\u305B"; break; // せ
            case 0x0F: y = "\u305D"; break; // そ
            case 0x10: y = "\u305F"; break; // た
            case 0x11: y = "\u3061"; break; // ち
            case 0x12: y = "\u3064"; break; // つ
            case 0x13: y = "\u3066"; break; // て
            case 0x14: y = "\u3068"; break; // と
            case 0x15: y = "\u306A"; break; // な
            case 0x16: y = "\u306B"; break; // に
            case 0x17: y = "\u306C"; break; // ぬ
            case 0x18: y = "\u306D"; break; // ね
            case 0x19: y = "\u306E"; break; // の
            case 0x1A: y = "\u306F"; break; // は
            case 0x1B: y = "\u3072"; break; // ひ
            case 0x1C: y = "\u3075"; break; // ふ
            case 0x1D: y = "\u3078"; break; // へ
            case 0x1E: y = "\u307B"; break; // ほ
            case 0x1F: y = "\u307E"; break; // ま
            case 0x20: y = "\u307F"; break; // み
            case 0x21: y = "\u3080"; break; // む
            case 0x22: y = "\u3081"; break; // め
            case 0x23: y = "\u3082"; break; // も
            case 0x24: y = "\u3084"; break; // や
            case 0x25: y = "\u3086"; break; // ゆ
            case 0x26: y = "\u3088"; break; // よ
            case 0x27: y = "\u3089"; break; // ら
            case 0x28: y = "\u308A"; break; // り
            case 0x29: y = "\u308B"; break; // る
            case 0x2A: y = "\u308C"; break; // れ
            case 0x2B: y = "\u308D"; break; // ろ
            case 0x2C: y = "\u308F"; break; // わ
            case 0x2D: y = "\u3092"; break; // を
            case 0x2E: y = "\u3093"; break; // ん
            case 0x2F: y = "\u3041"; break; // ぁ
            case 0x30: y = "\u3043"; break; // ぃ
            case 0x31: y = "\u3045"; break; // ぅ
            case 0x32: y = "\u3047"; break; // ぇ
            case 0x33: y = "\u3049"; break; // ぉ
            case 0x34: y = "\u3083"; break; // ゃ
            case 0x35: y = "\u3085"; break; // ゅ
            case 0x36: y = "\u3087"; break; // ょ
            case 0x37: y = "\u304C"; break; // が
            case 0x38: y = "\u304E"; break; // ぎ
            case 0x39: y = "\u3050"; break; // ぐ
            case 0x3A: y = "\u3052"; break; // げ
            case 0x3B: y = "\u3054"; break; // ご
            case 0x3C: y = "\u3056"; break; // ざ
            case 0x3D: y = "\u3058"; break; // じ
            case 0x3E: y = "\u305A"; break; // ず
            case 0x3F: y = "\u305C"; break; // ぜ
            case 0x40: y = "\u305E"; break; // ぞ
            case 0x41: y = "\u3060"; break; // だ
            case 0x42: y = "\u3062"; break; // ぢ
            case 0x43: y = "\u3065"; break; // づ
            case 0x44: y = "\u3067"; break; // で
            case 0x45: y = "\u3069"; break; // ど
            case 0x46: y = "\u3070"; break; // ば
            case 0x47: y = "\u3073"; break; // び
            case 0x48: y = "\u3076"; break; // ぶ
            case 0x49: y = "\u3079"; break; // べ
            case 0x4A: y = "\u307C"; break; // ぼ
            case 0x4B: y = "\u3071"; break; // ぱ
            case 0x4C: y = "\u3074"; break; // ぴ
            case 0x4D: y = "\u3077"; break; // ぷ
            case 0x4E: y = "\u307A"; break; // ぺ
            case 0x4F: y = "\u307D"; break; // ぽ
            case 0x50: y = "\u3063"; break; // っ
            case 0x51: y = "\u30A2"; break; // ア
            case 0x52: y = "\u30A4"; break; // イ
            case 0x53: y = "\u30A6"; break; // ウ
            case 0x54: y = "\u30A8"; break; // エ
            case 0x55: y = "\u30AA"; break; // オ
            case 0x56: y = "\u30AB"; break; // カ
            case 0x57: y = "\u30AD"; break; // キ
            case 0x58: y = "\u30AF"; break; // ク
            case 0x59: y = "\u30B1"; break; // ケ
            case 0x5A: y = "\u30B3"; break; // コ
            case 0x5B: y = "\u30B5"; break; // サ
            case 0x5C: y = "\u30B7"; break; // シ
            case 0x5D: y = "\u30B9"; break; // ス
            case 0x5E: y = "\u30BB"; break; // セ
            case 0x5F: y = "\u30BD"; break; // ソ
            case 0x60: y = "\u30BF"; break; // タ
            case 0x61: y = "\u30C1"; break; // チ
            case 0x62: y = "\u30C4"; break; // ツ
            case 0x63: y = "\u30C6"; break; // テ
            case 0x64: y = "\u30C8"; break; // ト
            case 0x65: y = "\u30CA"; break; // ナ
            case 0x66: y = "\u30CB"; break; // ニ
            case 0x67: y = "\u30CC"; break; // ヌ
            case 0x68: y = "\u30CD"; break; // ネ
            case 0x69: y = "\u30CE"; break; // ノ
            case 0x6A: y = "\u30CF"; break; // ハ
            case 0x6B: y = "\u30D2"; break; // ヒ
            case 0x6C: y = "\u30D5"; break; // フ
            case 0x6D: y = "\u30D8"; break; // ヘ
            case 0x6E: y = "\u30DB"; break; // ホ
            case 0x6F: y = "\u30DE"; break; // マ
            case 0x70: y = "\u30DF"; break; // ミ
            case 0x71: y = "\u30E0"; break; // ム
            case 0x72: y = "\u30E1"; break; // メ
            case 0x73: y = "\u30E2"; break; // モ
            case 0x74: y = "\u30E4"; break; // ヤ
            case 0x75: y = "\u30E6"; break; // ユ
            case 0x76: y = "\u30E8"; break; // ヨ
            case 0x77: y = "\u30E9"; break; // ラ
            case 0x78: y = "\u30EA"; break; // リ
            case 0x79: y = "\u30EB"; break; // ル
            case 0x7A: y = "\u30EC"; break; // レ
            case 0x7B: y = "\u30ED"; break; // ロ
            case 0x7C: y = " ";     break;
            case 0x7D: y = "\u30F2"; break; // ヲ
            case 0x7E: y = "\u30F3"; break; // ン
            case 0x7F: y = "\u30A1"; break; // ァ
            case 0x80: y = "";      break;
            case 0x81: y = "\u30A5"; break; // ゥ
            case 0x82: y = "\u30A7"; break; // ェ
            case 0x83: y = "\u30A9"; break; // ォ
            case 0x84: y = "\u30E3"; break; // ャ
            case 0x85: y = "\u30E5"; break; // ュ
            case 0x86: y = "\u30E7"; break; // ョ
            case 0x87: y = "\u30AC"; break; // ガ
            case 0x88: y = "\u30AE"; break; // ギ
            case 0x89: y = "\u30B0"; break; // グ
            case 0x8A: y = "\u30B2"; break; // ゲ
            case 0x8B: y = "\u30B4"; break; // ゴ
            case 0x8C: y = "\u30B6"; break; // ザ
            case 0x8D: y = "\u30B8"; break; // ジ
            case 0x8E: y = "\u30BA"; break; // ズ
            case 0x8F: y = "\u30BC"; break; // ゼ
            case 0x90: y = "\u30BE"; break; // ゾ
            case 0x91: y = "\u30C0"; break; // ダ
            case 0x92: y = "\u30C2"; break; // ヂ
            case 0x93: y = "\u30C5"; break; // ヅ
            case 0x94: y = "\u30C7"; break; // デ
            case 0x95: y = "\u30C9"; break; // ド
            case 0x96: y = "\u30D0"; break; // バ
            case 0x97: y = "\u30D3"; break; // ビ
            case 0x98: y = "\u30D6"; break; // ブ
            case 0x99: y = "\u30D9"; break; // ベ
            case 0x9A: y = "\u30DC"; break; // ボ
            case 0x9B: y = "\u30D1"; break; // パ
            case 0x9C: y = "\u30D4"; break; // ピ
            case 0x9D: y = "\u30D7"; break; // プ
            case 0x9E: y = "\u30DA"; break; // ペ
            case 0x9F: y = "\u30DD"; break; // ポ
            case 0xA0: y = "\u30C3"; break; // ッ
            case 0xA1: y = "0"; break;
            case 0xA2: y = "1"; break;
            case 0xA3: y = "2"; break;
            case 0xA4: y = "3"; break;
            case 0xA5: y = "4"; break;
            case 0xA6: y = "5"; break;
            case 0xA7: y = "6"; break;
            case 0xA8: y = "7"; break;
            case 0xA9: y = "8"; break;
            case 0xAA: y = "9"; break;
            case 0xAB: y = "!"; break;
            case 0xAC: y = "?"; break;
            case 0xAD: y = "."; break;
            case 0xAE: y = "\u30FC"; break; // ー
            case 0xAF: y = "\uFF77"; break; // ｷ
            case 0xFA: y = "\\l"; break;
            case 0xFB: y = "\\p"; break;
            case 0xFC: y = "\\c"; nextIsHex = true; break; // Japanese FC: raw hex follows
            case 0xFD: y = "\\v"; nextIsHex = true; break; // Japanese FD: raw hex follows
            case 0xFE: y = "\\n"; break;
            default:
                y = QString("\\h%1").arg(x, 2, 16, QChar('0')).toUpper();
                break;
            }
        } else {
            // Non-Japanese (English/European) table
            switch (x) {
            case 0x00: y = " ";   break;
            case 0x01: y = "\u00C0"; break; // À
            case 0x02: y = "\u00C1"; break; // Á
            case 0x03: y = "\u00C2"; break; // Â
            case 0x04: y = "\u00C7"; break; // Ç
            case 0x05: y = "\u00C8"; break; // È
            case 0x06: y = "\u00C9"; break; // É
            case 0x07: y = "\u00CA"; break; // Ê
            case 0x08: y = "\u00CB"; break; // Ë
            case 0x09: y = "\u00CC"; break; // Ì
            case 0x0B: y = "\u00CE"; break; // Î
            case 0x0C: y = "\u00CF"; break; // Ï
            case 0x0D: y = "\u00D2"; break; // Ò
            case 0x0E: y = "\u00D3"; break; // Ó
            case 0x0F: y = "\u00D4"; break; // Ô
            case 0x10: y = "\u0152"; break; // Œ
            case 0x11: y = "\u00D9"; break; // Ù
            case 0x12: y = "\u00DA"; break; // Ú
            case 0x13: y = "\u00DB"; break; // Û
            case 0x15: y = "\u00DF"; break; // ß
            case 0x16: y = "\u00E0"; break; // à
            case 0x17: y = "\u00E1"; break; // á
            case 0x19: y = "\u00E7"; break; // ç
            case 0x1A: y = "\u00E8"; break; // è
            case 0x1B: y = "\u00E9"; break; // é
            case 0x1C: y = "\u00EA"; break; // ê
            case 0x1D: y = "\u00EB"; break; // ë
            case 0x1E: y = "\u00EC"; break; // ì
            case 0x20: y = "\u00EE"; break; // î
            case 0x21: y = "\u00EF"; break; // ï
            case 0x22: y = "\u00F2"; break; // ò
            case 0x23: y = "\u00F3"; break; // ó
            case 0x24: y = "\u0153"; break; // œ
            case 0x25: y = "\u00F9"; break; // ù
            case 0x26: y = "\u00FA"; break; // ú
            case 0x28: y = "\u00B0"; break; // °
            case 0x29: y = "\u00AA"; break; // ª
            case 0x2B: y = "&";    break;
            case 0x2C: y = "+";    break;
            case 0x2D: y = "&";    break;
            case 0x34: y = "[Lv]"; break;
            case 0x35: y = "=";    break;
            case 0x51: y = "\u00BF"; break; // ¿
            case 0x52: y = "\u00A1"; break; // ¡
            case 0x53: y = "[PK]"; break;
            case 0x54: y = "[MN]"; break;
            case 0x55: y = "[PO]"; break;
            case 0x56: y = "[Ke]"; break;
            case 0x57: y = "[BL]"; break;
            case 0x58: y = "[OC]"; break;
            case 0x59: y = "[K]";  break;
            case 0x5A: y = "\u00CD"; break; // Í
            case 0x5B: y = "%";    break;
            case 0x5C: y = "(";    break;
            case 0x5D: y = ")";    break;
            case 0x68: y = "\u00E2"; break; // â
            case 0x6F: y = "\u00ED"; break; // í
            case 0x79: y = "[U]";  break;
            case 0x7A: y = "[D]";  break;
            case 0x7B: y = "[L]";  break;
            case 0x7C: y = "[R]";  break;
            case 0xA1: y = "0";    break;
            case 0xA2: y = "1";    break;
            case 0xA3: y = "2";    break;
            case 0xA4: y = "3";    break;
            case 0xA5: y = "4";    break;
            case 0xA6: y = "5";    break;
            case 0xA7: y = "6";    break;
            case 0xA8: y = "7";    break;
            case 0xA9: y = "8";    break;
            case 0xAA: y = "9";    break;
            case 0xAB: y = "!";    break;
            case 0xAC: y = "?";    break;
            case 0xAD: y = ".";    break;
            case 0xAE: y = "-";    break;
            case 0xAF: y = "\u00B7"; break; // ·
            case 0xB0: y = "[.]";  break;
            case 0xB1: y = "[\"]"; break;
            case 0xB2: y = "\"";   break;
            case 0xB3: y = "[']";  break;
            case 0xB4: y = "'";    break;
            case 0xB5: y = "\u2642"; break; // ♂
            case 0xB6: y = "\u2640"; break; // ♀
            case 0xB7: y = "[p]";  break;
            case 0xB8: y = ",";    break;
            case 0xB9: y = "[x]";  break;
            case 0xBA: y = "/";    break;
            case 0xBB: y = "A";    break;
            case 0xBC: y = "B";    break;
            case 0xBD: y = "C";    break;
            case 0xBE: y = "D";    break;
            case 0xBF: y = "E";    break;
            case 0xC0: y = "F";    break;
            case 0xC1: y = "G";    break;
            case 0xC2: y = "H";    break;
            case 0xC3: y = "I";    break;
            case 0xC4: y = "J";    break;
            case 0xC5: y = "K";    break;
            case 0xC6: y = "L";    break;
            case 0xC7: y = "M";    break;
            case 0xC8: y = "N";    break;
            case 0xC9: y = "O";    break;
            case 0xCA: y = "P";    break;
            case 0xCB: y = "Q";    break;
            case 0xCC: y = "R";    break;
            case 0xCD: y = "S";    break;
            case 0xCE: y = "T";    break;
            case 0xCF: y = "U";    break;
            case 0xD0: y = "V";    break;
            case 0xD1: y = "W";    break;
            case 0xD2: y = "X";    break;
            case 0xD3: y = "Y";    break;
            case 0xD4: y = "Z";    break;
            case 0xD5: y = "a";    break;
            case 0xD6: y = "b";    break;
            case 0xD7: y = "c";    break;
            case 0xD8: y = "d";    break;
            case 0xD9: y = "e";    break;
            case 0xDA: y = "f";    break;
            case 0xDB: y = "g";    break;
            case 0xDC: y = "h";    break;
            case 0xDD: y = "i";    break;
            case 0xDE: y = "j";    break;
            case 0xDF: y = "k";    break;
            case 0xE0: y = "l";    break;
            case 0xE1: y = "m";    break;
            case 0xE2: y = "n";    break;
            case 0xE3: y = "o";    break;
            case 0xE4: y = "p";    break;
            case 0xE5: y = "q";    break;
            case 0xE6: y = "r";    break;
            case 0xE7: y = "s";    break;
            case 0xE8: y = "t";    break;
            case 0xE9: y = "u";    break;
            case 0xEA: y = "v";    break;
            case 0xEB: y = "w";    break;
            case 0xEC: y = "x";    break;
            case 0xED: y = "y";    break;
            case 0xEE: y = "z";    break;
            case 0xEF: y = "[>]";  break;
            case 0xF0: y = ":";    break;
            case 0xF1: y = "\u00C4"; break; // Ä
            case 0xF2: y = "\u00D6"; break; // Ö
            case 0xF3: y = "\u00DC"; break; // Ü
            case 0xF4: y = "\u00E4"; break; // ä
            case 0xF5: y = "\u00F6"; break; // ö
            case 0xF6: y = "\u00FC"; break; // ü
            case 0xF7: y = "[u]";  break;
            case 0xF8: y = "[d]";  break;
            case 0xF9: y = "[l]";  break;
            case 0xFA: y = "\\l";  break;
            case 0xFB: y = "\\p";  break;
            case 0xFC: {
                // Multi-byte control code: FC XX [params...]
                if (idx + 1 < bytes.size()) {
                    unsigned char sub = static_cast<unsigned char>(bytes[++idx]);
                    const char *name = nullptr;
                    int extra = 0;
                    switch (sub) {
                    case 0x00: name = "NAME_END";              extra = 0; break;
                    case 0x01: name = "COLOR";                 extra = 1; break;
                    case 0x02: name = "HIGHLIGHT";             extra = 1; break;
                    case 0x03: name = "SHADOW";                extra = 1; break;
                    case 0x04: name = "COLOR_HIGHLIGHT_SHADOW";extra = 3; break;
                    case 0x05: name = "PALETTE";               extra = 1; break;
                    case 0x06: name = "FONT";                  extra = 1; break;
                    case 0x07: name = "RESET_FONT";            extra = 0; break;
                    case 0x08: name = "PAUSE";                 extra = 1; break;
                    case 0x09: name = "PAUSE_UNTIL_PRESS";     extra = 0; break;
                    case 0x0A: name = "WAIT_SE";               extra = 0; break;
                    case 0x0B: name = "PLAY_BGM";              extra = 2; break;
                    case 0x0C: name = "ESCAPE";                extra = 0; break;
                    case 0x0D: name = "SHIFT_RIGHT";           extra = 1; break;
                    case 0x0E: name = "SHIFT_DOWN";            extra = 1; break;
                    case 0x0F: name = "FILL_WINDOW";           extra = 0; break;
                    case 0x10: name = "PLAY_SE";               extra = 2; break;
                    case 0x11: name = "CLEAR";                 extra = 1; break;
                    case 0x12: name = "SKIP";                  extra = 1; break;
                    case 0x13: name = "CLEAR_TO";              extra = 1; break;
                    case 0x14: name = "MIN_LETTER_SPACING";    extra = 1; break;
                    case 0x15: name = "JPN";                   extra = 0; break;
                    case 0x16: name = "ENG";                   extra = 0; break;
                    case 0x17: name = "PAUSE_MUSIC";           extra = 0; break;
                    case 0x18: name = "RESUME_MUSIC";          extra = 0; break;
                    default:   break;
                    }
                    if (name) {
                        y = QString("{%1}").arg(name);
                        if (extra == 1 && idx + 1 < bytes.size()) {
                            unsigned char p = static_cast<unsigned char>(bytes[++idx]);
                            y += QString("{0x%1}").arg(p, 2, 16, QChar('0')).toUpper();
                        } else if (extra == 2 && idx + 2 < bytes.size()) {
                            unsigned char lo = static_cast<unsigned char>(bytes[++idx]);
                            unsigned char hi = static_cast<unsigned char>(bytes[++idx]);
                            uint16_t val = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
                            y += "{0x" + QString::number(val, 16).toUpper().rightJustified(4, '0') + "}";
                        } else if (extra == 3 && idx + 3 < bytes.size()) {
                            for (int e = 0; e < 3; e++) {
                                unsigned char p = static_cast<unsigned char>(bytes[++idx]);
                                y += QString("{0x%1}").arg(p, 2, 16, QChar('0')).toUpper();
                            }
                        }
                    } else {
                        y = QString("{FC_%1}").arg(sub, 2, 16, QChar('0')).toUpper();
                    }
                }
                break;
            }
            case 0xFD: {
                // Variable substitution: next byte is buffer index
                if (idx + 1 < bytes.size()) {
                    unsigned char varIdx = static_cast<unsigned char>(bytes[++idx]);
                    switch (varIdx) {
                    case 0x01: y = "{PLAYER}";         break;
                    case 0x02: y = "{STR_VAR_1}";      break;
                    case 0x03: y = "{STR_VAR_2}";      break;
                    case 0x04: y = "{STR_VAR_3}";      break;
                    case 0x05: y = "{KUN}";             break;
                    case 0x06: y = "{RIVAL}";           break;
                    case 0x07: y = "{VERSION}";         break;
                    case 0x08: y = "{EVIL_TEAM}";       break;
                    case 0x09: y = "{GOOD_TEAM}";       break;
                    case 0x0A: y = "{EVIL_LEADER}";     break;
                    case 0x0B: y = "{GOOD_LEADER}";     break;
                    case 0x0C: y = "{EVIL_LEGENDARY}";  break;
                    case 0x0D: y = "{GOOD_LEGENDARY}";  break;
                    default:
                        y = QString("{STR_VAR_%1}").arg(varIdx, 2, 16, QChar('0')).toUpper();
                        break;
                    }
                }
                break;
            }
            case 0xFE: y = "\\n";  break;
            default:
                y = QString("\\h%1").arg(x, 2, 16, QChar('0')).toUpper();
                break;
            }
        }

        result += y;
    }

    return result;
}
