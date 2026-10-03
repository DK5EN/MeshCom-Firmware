// Native Testsuite fuer compress_functions (BACKLOG W0.6). Die Funktionen
// komprimieren einen String, indem sie das ertragreichste Teilmuster durch ein
// einzelnes Zeichen ersetzen ("<muster>:<text mit #>"), und expandieren es
// wieder. Die Quelle sind die beiden Upstream-Hilfskopien in diesem
// Verzeichnis (compress_functions.cpp/.h, hier unveraendert gelassen).
//
// Herkunft / Abgleich mit src/: src/ enthaelt KEINE compress_functions und
// ruft sie nirgends auf -- der einzige Treffer ist das auskommentierte
// "//TEST #include "compress_functions.h"" in src/command_functions.cpp. Die
// Kopien hier sind also nicht Produktivcode, sondern ein toter Upstream-Rest;
// ein Diff gegen src/ ist mangels Gegenstueck nicht moeglich. Dieser Test
// pinnt das tatsaechliche Verhalten (Charakterisierung), damit eine spaetere
// Aktivierung der Funktionen nicht ungeprueft auf die unten dokumentierten
// Round-Trip-Luecken laeuft.
//
// Die Round-Trip-Eigenschaft decode(encode(x)) == x gilt nur, wenn x weder
// das Kompressionszeichen noch ':' in einem Muster enthaelt (Format ohne
// Escaping). Die beiden Verletzungen sind als BUG markiert und pinnen das
// heutige Fehlverhalten; sie sind kein Sollverhalten.
//
//   pio test -e native -f test_compress

#include <unity.h>

#include <Arduino.h>
#include "compress_functions.h"

void setUp(void) {}
void tearDown(void) {}

static String roundtrip(const String &in, const char *cc = "#")
{
    return compress_decode(compress_encode(in, cc), cc);
}

static void test_roundtrip_empty(void)
{
    TEST_ASSERT_EQUAL_STRING(":", compress_encode("", "#").c_str());
    TEST_ASSERT_EQUAL_STRING("", roundtrip("").c_str());
}

// Kein Teilmuster spart etwas: leeres Muster, Text bleibt, Praefix ":".
static void test_roundtrip_no_substitutable_pattern(void)
{
    TEST_ASSERT_EQUAL_STRING(":abcdefg", compress_encode("abcdefg", "#").c_str());
    TEST_ASSERT_EQUAL_STRING("abcdefg", roundtrip("abcdefg").c_str());
}

// Ein einzelnes Zeichen / zwei Zeichen: Mustersuche beginnt erst bei Laenge 2
// und endet bei length/2, also gibt es nichts zu ersetzen.
static void test_roundtrip_tiny_inputs(void)
{
    TEST_ASSERT_EQUAL_STRING("a", roundtrip("a").c_str());
    TEST_ASSERT_EQUAL_STRING("ab", roundtrip("ab").c_str());
}

// Nur aus dem Muster aufgebaut: "ab" x4 -> Ersparnis 1, erstes Maximum "ab".
static void test_roundtrip_only_pattern(void)
{
    String c = compress_encode("abababab", "#");
    TEST_ASSERT_EQUAL_STRING("ab:####", c.c_str());
    TEST_ASSERT_TRUE(c.length() < String("abababab").length());
    TEST_ASSERT_EQUAL_STRING("abababab", compress_decode(c, "#").c_str());

    c = compress_encode("hellohello", "#");
    TEST_ASSERT_EQUAL_STRING("hello:##", c.c_str());
    TEST_ASSERT_EQUAL_STRING("hellohello", compress_decode(c, "#").c_str());
}

// Gemischter Text mit wiederholtem Muster in Prosa.
static void test_roundtrip_mixed_text(void)
{
    const String in = "CQ CQ CQ de DK5EN CQ CQ";
    const String c = compress_encode(in, "#");
    TEST_ASSERT_TRUE(c.length() < in.length());
    TEST_ASSERT_EQUAL_STRING(in.c_str(), compress_decode(c, "#").c_str());
}

// Maximale Textlaenge einer MeshCom-Nachricht (149 Zeichen) -- Laufzeit der
// O(n^3)-Mustersuche bleibt hier trivial; Round-Trip und Verkleinerung.
static void test_roundtrip_max_length_input(void)
{
    String in;
    while (in.length() < 149)
        in.concat("DK5EN-99 ");
    in = in.substring(0, 149);
    TEST_ASSERT_EQUAL_UINT(149, in.length());

    const String c = compress_encode(in, "#");
    TEST_ASSERT_TRUE(c.length() < in.length());
    TEST_ASSERT_EQUAL_STRING(in.c_str(), compress_decode(c, "#").c_str());
}

// Anderes Kompressionszeichen als '#'.
static void test_roundtrip_custom_compress_char(void)
{
    const String c = compress_encode("abababab", "~");
    TEST_ASSERT_EQUAL_STRING("ab:~~~~", c.c_str());
    TEST_ASSERT_EQUAL_STRING("abababab", compress_decode(c, "~").c_str());
}

// BUG (Upstream, kein Escaping): enthaelt der Text das Kompressionszeichen
// selbst und es gibt kein Muster, ist das Muster leer; decode ersetzt dann
// jedes '#' durch "" und verschluckt es still. Round-Trip bricht.
static void test_bug_substitute_char_in_text_without_pattern_is_dropped(void)
{
    TEST_ASSERT_EQUAL_STRING(":a#b", compress_encode("a#b", "#").c_str());
    TEST_ASSERT_EQUAL_STRING("ab", roundtrip("a#b").c_str()); // Soll waere "a#b"
}

// BUG (Upstream, kein Escaping): mit Muster werden vorhandene '#' im Text
// ebenfalls zu Mustern expandiert -- der Text waechst und ist falsch.
static void test_bug_substitute_char_in_text_with_pattern_is_expanded(void)
{
    const String in = "abab#abab#abab";
    const String c = compress_encode(in, "#");
    TEST_ASSERT_EQUAL_STRING("abab:#####", c.c_str());
    TEST_ASSERT_EQUAL_STRING("abababababababababab", compress_decode(c, "#").c_str());
    TEST_ASSERT_TRUE(roundtrip(in) != in); // Soll waere Gleichheit
}

// BUG (Upstream): decode trennt am ERSTEN ':' des komprimierten Strings. Ein
// Muster mit ':' ("a:b") wird dadurch abgeschnitten, der Rest wandert in den
// Text. Round-Trip bricht.
static void test_bug_colon_inside_pattern_breaks_decode(void)
{
    const String in = "a:ba:ba:b";
    const String c = compress_encode(in, "#");
    TEST_ASSERT_EQUAL_STRING("a:b:###", c.c_str());
    TEST_ASSERT_EQUAL_STRING("b:aaa", compress_decode(c, "#").c_str()); // Soll waere "a:ba:ba:b"
}

// Doppelpunkt im Text, aber nicht im Muster, ist unkritisch: das Muster steht
// vor dem ersten ':' und der Rest bleibt unangetastet.
static void test_colon_in_text_outside_pattern_is_fine(void)
{
    const String in = "abababab:xyz";
    TEST_ASSERT_EQUAL_STRING(in.c_str(), roundtrip(in).c_str());
}

static void test_get_occurrences_is_non_overlapping(void)
{
    TEST_ASSERT_EQUAL_INT(2, getOccurences("aaaa", "aa"));
    TEST_ASSERT_EQUAL_INT(1, getOccurences("aaa", "aa"));
    TEST_ASSERT_EQUAL_INT(0, getOccurences("abc", "zz"));
    TEST_ASSERT_EQUAL_INT(0, getOccurences("abc", ""));
    TEST_ASSERT_EQUAL_INT(0, getOccurences("", "ab"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_empty);
    RUN_TEST(test_roundtrip_no_substitutable_pattern);
    RUN_TEST(test_roundtrip_tiny_inputs);
    RUN_TEST(test_roundtrip_only_pattern);
    RUN_TEST(test_roundtrip_mixed_text);
    RUN_TEST(test_roundtrip_max_length_input);
    RUN_TEST(test_roundtrip_custom_compress_char);
    RUN_TEST(test_bug_substitute_char_in_text_without_pattern_is_dropped);
    RUN_TEST(test_bug_substitute_char_in_text_with_pattern_is_expanded);
    RUN_TEST(test_bug_colon_inside_pattern_breaks_decode);
    RUN_TEST(test_colon_in_text_outside_pattern_is_fine);
    RUN_TEST(test_get_occurrences_is_non_overlapping);
    return UNITY_END();
}
