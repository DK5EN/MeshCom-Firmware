// R2-04. Die Textoperationen, die beim Umstieg von Arduino-String auf feste
// char[] uebrig bleiben. Geprueft wird hier vor allem das, was eine
// handgeschriebene Fassung je Aufrufstelle falsch machen wuerde: die GRENZEN.

#include <unity.h>
#include <string.h>

#include "../../src/mc_text.h"

void setUp(void) {}
void tearDown(void) {}

// --- mcAppend: alles oder nichts ----------------------------------------

void test_append_that_fits_exactly(void)
{
    char b[8] = "abc";
    TEST_ASSERT_TRUE(mcAppend(b, sizeof(b), "defg"));   // 3+4+NUL == 8
    TEST_ASSERT_EQUAL_STRING("abcdefg", b);
}

void test_append_one_byte_too_long_changes_nothing(void)
{
    // Der entscheidende Fall. strncat() wuerde hier KUERZEN und ein halbes
    // Rufzeichen in den Pfad schreiben; das waere ein anderes Rufzeichen.
    char b[8] = "abc";
    TEST_ASSERT_FALSE(mcAppend(b, sizeof(b), "defgh"));
    TEST_ASSERT_EQUAL_STRING("abc", b);                 // unveraendert
}

void test_append_to_full_buffer_changes_nothing(void)
{
    char b[4] = "abc";
    TEST_ASSERT_FALSE(mcAppend(b, sizeof(b), "x"));
    TEST_ASSERT_EQUAL_STRING("abc", b);
}

void test_append_empty_string_succeeds(void)
{
    char b[4] = "ab";
    TEST_ASSERT_TRUE(mcAppend(b, sizeof(b), ""));
    TEST_ASSERT_EQUAL_STRING("ab", b);
}

void test_append_char_respects_the_same_boundary(void)
{
    char b[4] = "abc";
    TEST_ASSERT_FALSE(mcAppendChar(b, sizeof(b), 'd'));
    TEST_ASSERT_EQUAL_STRING("abc", b);
    char c[5] = "abc";
    TEST_ASSERT_TRUE(mcAppendChar(c, sizeof(c), 'd'));
    TEST_ASSERT_EQUAL_STRING("abcd", c);
}

void test_append_to_unterminated_buffer_is_refused(void)
{
    // Kein NUL im Puffer: strlen() wuerde hier ueber das Ende hinauslaufen.
    char b[4] = {'a','b','c','d'};
    TEST_ASSERT_FALSE(mcAppend(b, sizeof(b), "x"));
    TEST_ASSERT_EQUAL_UINT8('d', (uint8_t)b[3]);        // nichts angefasst
}

void test_append_null_arguments_are_refused(void)
{
    char b[4] = "ab";
    TEST_ASSERT_FALSE(mcAppend(0, 4, "x"));
    TEST_ASSERT_FALSE(mcAppend(b, sizeof(b), 0));
    TEST_ASSERT_FALSE(mcAppend(b, 0, "x"));
    TEST_ASSERT_EQUAL_STRING("ab", b);
}

// --- der Pfad-Anhang, wie ihn die Aufrufstellen benutzen ------------------

void test_path_append_is_the_real_call_site(void)
{
    // lora_functions.cpp / udp_frame_*.cpp: ",<eigenes Rufzeichen>" an den
    // Quellpfad. So sieht der Hop aus, den dieser Knoten hinzufuegt.
    char path[121] = "OE1ABC-1,OE3XYZ-2";   // MC_PATH_LEN, hier ohne Arduino-Header
    TEST_ASSERT_TRUE(mcAppendChar(path, sizeof(path), ','));
    TEST_ASSERT_TRUE(mcAppend(path, sizeof(path), "DK5EN-90"));
    TEST_ASSERT_EQUAL_STRING("OE1ABC-1,OE3XYZ-2,DK5EN-90", path);
}

void test_path_append_at_the_brim_leaves_the_path_valid(void)
{
    // Ein voller Pfad darf nicht halb ergaenzt werden: entweder der Hop steht
    // ganz drin, oder der Pfad bleibt der alte gueltige.
    char path[12] = "OE1ABC-1";
    TEST_ASSERT_TRUE(mcAppendChar(path, sizeof(path), ','));
    TEST_ASSERT_FALSE(mcAppend(path, sizeof(path), "DK5EN-90"));
    TEST_ASSERT_EQUAL_STRING("OE1ABC-1,", path);
}

// --- mcSliceToLong: das substring(a,b).toInt()-Muster ---------------------

void test_slice_reads_a_fixed_width_field(void)
{
    // loop_functions.cpp:2395-2401 liest so Datum und Uhrzeit aus dem Text.
    const char *s = "TIME 2026-09-17 07:05:09";
    TEST_ASSERT_EQUAL_INT32(2026, mcSliceToLong(s, 5, 9));
    TEST_ASSERT_EQUAL_INT32(9,    mcSliceToLong(s, 10, 12));
    TEST_ASSERT_EQUAL_INT32(17,   mcSliceToLong(s, 13, 15));
    TEST_ASSERT_EQUAL_INT32(7,    mcSliceToLong(s, 16, 18));
    TEST_ASSERT_EQUAL_INT32(5,    mcSliceToLong(s, 19, 21));
    TEST_ASSERT_EQUAL_INT32(9,    mcSliceToLong(s, 22, 24));
}

void test_slice_past_the_end_yields_zero_not_garbage(void)
{
    TEST_ASSERT_EQUAL_INT32(0, mcSliceToLong("abc", 10, 12));
    TEST_ASSERT_EQUAL_INT32(0, mcSliceToLong("", 0, 4));
    TEST_ASSERT_EQUAL_INT32(0, mcSliceToLong(0, 0, 4));
}

void test_slice_clipped_by_the_end_reads_what_is_there(void)
{
    // substring() kuerzt still; das muss hier genauso sein.
    TEST_ASSERT_EQUAL_INT32(42, mcSliceToLong("x42", 1, 99));
}

void test_slice_to_before_from_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(0, mcSliceToLong("12345", 3, 1));
}

void test_slice_of_the_ack_tail(void)
{
    // lora_functions.cpp:1057: substring(iAckPos+4).toInt() -- Rest der Zeile.
    const char *s = "text{ack12345";
    int pos = mcIndexOf(s, '{');
    TEST_ASSERT_EQUAL_INT(4, pos);
    TEST_ASSERT_EQUAL_INT32(12345, mcSliceToLong(s, (size_t)pos + 4, strlen(s)));
}

// --- mcStartsWith / mcIndexOf -------------------------------------------

void test_starts_with(void)
{
    TEST_ASSERT_TRUE(mcStartsWith("PONG abc", "PONG"));
    TEST_ASSERT_FALSE(mcStartsWith("PON", "PONG"));      // kuerzer als Praefix
    TEST_ASSERT_TRUE(mcStartsWith("abc", ""));
    TEST_ASSERT_FALSE(mcStartsWith(0, "x"));
}

void test_index_of_matches_arduino_semantics(void)
{
    TEST_ASSERT_EQUAL_INT(0, mcIndexOf("abc", 'a'));
    TEST_ASSERT_EQUAL_INT(2, mcIndexOf("abc", 'c'));
    TEST_ASSERT_EQUAL_INT(-1, mcIndexOf("abc", 'z'));
    TEST_ASSERT_EQUAL_INT(-1, mcIndexOf("", 'a'));
    TEST_ASSERT_EQUAL_INT(1, mcIndexOfStr("xabc", "abc"));
    TEST_ASSERT_EQUAL_INT(-1, mcIndexOfStr("xabc", "q"));
}

// --- mcTruncate: das s = s.substring(0, n)-Muster ------------------------

void test_truncate_cuts_and_shorter_is_left_alone(void)
{
    char b[16] = "abcdef";
    mcTruncate(b, sizeof(b), 3);
    TEST_ASSERT_EQUAL_STRING("abc", b);
    mcTruncate(b, sizeof(b), 99);                       // laenger als Inhalt
    TEST_ASSERT_EQUAL_STRING("abc", b);
}

// --- mcSet: die Zuweisung ------------------------------------------------

void test_set_copies_and_terminates(void)
{
    char b[8];
    memset(b, 0x7E, sizeof(b));
    TEST_ASSERT_TRUE(mcSet(b, sizeof(b), "abc"));
    TEST_ASSERT_EQUAL_STRING("abc", b);
}

void test_set_truncates_and_says_so(void)
{
    // Anders als mcAppend() kuerzt die Zuweisung -- aber sie meldet es, damit
    // eine Aufrufstelle, die das nicht hinnehmen darf, es merken kann.
    char b[4];
    TEST_ASSERT_FALSE(mcSet(b, sizeof(b), "abcdef"));
    TEST_ASSERT_EQUAL_STRING("abc", b);
    TEST_ASSERT_EQUAL_size_t(3u, strlen(b));
}

void test_set_null_source_empties_the_field(void)
{
    char b[8] = "abc";
    TEST_ASSERT_FALSE(mcSet(b, sizeof(b), 0));
    TEST_ASSERT_EQUAL_STRING("", b);
}

void test_index_of_from_returns_an_absolute_index(void)
{
    // udp_frame_*.cpp:229 sucht die ZWEITE '{'-Gruppe: indexOf("{", 1).
    // Arduino liefert dabei einen absoluten Index, keinen relativen.
    const char *s = "{ack}text{12345";
    TEST_ASSERT_EQUAL_INT(9, mcIndexOfStrFrom(s, "{", 1));
    TEST_ASSERT_EQUAL_INT(0, mcIndexOfStrFrom(s, "{", 0));
    TEST_ASSERT_EQUAL_INT(-1, mcIndexOfStrFrom(s, "{", 10));
    TEST_ASSERT_EQUAL_INT(-1, mcIndexOfStrFrom(s, "{", 999));   // from > len
    TEST_ASSERT_EQUAL_INT(-1, mcIndexOfStrFrom(0, "{", 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_append_that_fits_exactly);
    RUN_TEST(test_append_one_byte_too_long_changes_nothing);
    RUN_TEST(test_append_to_full_buffer_changes_nothing);
    RUN_TEST(test_append_empty_string_succeeds);
    RUN_TEST(test_append_char_respects_the_same_boundary);
    RUN_TEST(test_append_to_unterminated_buffer_is_refused);
    RUN_TEST(test_append_null_arguments_are_refused);
    RUN_TEST(test_path_append_is_the_real_call_site);
    RUN_TEST(test_path_append_at_the_brim_leaves_the_path_valid);
    RUN_TEST(test_slice_reads_a_fixed_width_field);
    RUN_TEST(test_slice_past_the_end_yields_zero_not_garbage);
    RUN_TEST(test_slice_clipped_by_the_end_reads_what_is_there);
    RUN_TEST(test_slice_to_before_from_is_zero);
    RUN_TEST(test_slice_of_the_ack_tail);
    RUN_TEST(test_starts_with);
    RUN_TEST(test_index_of_matches_arduino_semantics);
    RUN_TEST(test_index_of_from_returns_an_absolute_index);
    RUN_TEST(test_truncate_cuts_and_shorter_is_left_alone);
    RUN_TEST(test_set_copies_and_terminates);
    RUN_TEST(test_set_truncates_and_says_so);
    RUN_TEST(test_set_null_source_empties_the_field);
    return UNITY_END();
}
