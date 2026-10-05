// Native test for the RM GUI known-node store (contract C1): src/rm_nodes_store.h.
//
//   .../pio_locked.sh test -e native_rm_nodes_store
//
// Header-only, so the codec, the two-slot selection and the load/save sequences that ship in
// esp32_flash.cpp / nrf52_flash.cpp are exercised here as they are, over a fake storage (two files
// for the nRF52 layout, one blob for the ESP32 layout). What this does NOT cover: the Preferences /
// InternalFS primitives themselves and a real power cut (hardware bench).

#include <unity.h>

#include <stdint.h>
#include <string.h>
#include <vector>

#include <rm_nodes_store.h>

void setUp(void) {}
void tearDown(void) {}

// ---- helpers ------------------------------------------------------------------------------------

static RmNodes makeNodes(int count)
{
    RmNodes n;
    memset(&n, 0, sizeof(n));
    static const char *calls[3] = {"DK5EN-12", "OE1ABC", "DL1XYZ-99"};
    for (int i = 0; i < count && i < 3; i++)
    {
        n.slot[i].used = true;
        strncpy(n.slot[i].call, calls[i], RM_NODES_CALL_LEN - 1);
        for (int k = 0; k < RM_NODES_KEY_LEN; k++)
            n.slot[i].key[k] = (uint8_t)(0x10 * (i + 1) + k);
    }
    return n;
}

static void refreshCrc(uint8_t *rec)
{
    rmNodesPut32(rec + kRmNodesCrcOffset, crc32_buf(rec, kRmNodesCrcOffset));
}

static size_t encodeOk(const RmNodes &n, uint32_t seq, uint8_t *out)
{
    size_t len = rmNodesEncode(n, seq, out, kRmNodesRecSize);
    TEST_ASSERT_EQUAL_UINT32(kRmNodesRecSize, len);
    return len;
}

static void assertRejected(const uint8_t *rec, size_t len)
{
    RmNodes n = makeNodes(3); // preloaded with data: a reject must wipe it
    uint32_t seq = 77;
    TEST_ASSERT_FALSE(rmNodesDecode(rec, len, n, seq));
    TEST_ASSERT_EQUAL_UINT32(0, seq);
    RmNodes zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_MEMORY(&zero, &n, sizeof(n));
}

// ---- codec --------------------------------------------------------------------------------------

void test_layout_constants_and_golden_offsets(void)
{
    TEST_ASSERT_EQUAL_UINT32(43, kRmNodesSlotBytes);
    TEST_ASSERT_EQUAL_UINT32(141, kRmNodesCrcOffset);
    TEST_ASSERT_EQUAL_UINT32(145, kRmNodesRecSize);

    RmNodes n = makeNodes(2);
    uint8_t r[kRmNodesRecSize];
    encodeOk(n, 0x01020304UL, r);

    TEST_ASSERT_EQUAL_UINT8('R', r[0]);
    TEST_ASSERT_EQUAL_UINT8('M', r[1]);
    TEST_ASSERT_EQUAL_UINT8('N', r[2]);
    TEST_ASSERT_EQUAL_UINT8('1', r[3]);
    TEST_ASSERT_EQUAL_UINT8(1, r[4]);
    TEST_ASSERT_EQUAL_UINT8(3, r[5]);
    TEST_ASSERT_EQUAL_UINT8(0, r[6]);
    TEST_ASSERT_EQUAL_UINT8(0, r[7]);
    // seq little endian
    TEST_ASSERT_EQUAL_UINT8(0x04, r[8]);
    TEST_ASSERT_EQUAL_UINT8(0x03, r[9]);
    TEST_ASSERT_EQUAL_UINT8(0x02, r[10]);
    TEST_ASSERT_EQUAL_UINT8(0x01, r[11]);
    // slot 0: call, key, used flag
    TEST_ASSERT_EQUAL_STRING("DK5EN-12", (const char *)(r + 12));
    TEST_ASSERT_EQUAL_UINT8(0x10, r[22]);
    TEST_ASSERT_EQUAL_UINT8(0x10 + 31, r[22 + 31]);
    TEST_ASSERT_EQUAL_UINT8(1, r[12 + 42]);
    // slot 1 starts 43 bytes later
    TEST_ASSERT_EQUAL_STRING("OE1ABC", (const char *)(r + 12 + 43));
    // slot 2 unused: all 43 bytes zero
    for (size_t i = 12 + 86; i < 12 + 129; i++)
        TEST_ASSERT_EQUAL_UINT8(0, r[i]);
    // CRC little endian over the first 141 bytes
    uint32_t crc = crc32_buf(r, 141);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)crc, r[141]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(crc >> 24), r[144]);
}

void test_roundtrip_all_slot_counts(void)
{
    for (int count = 0; count <= 3; count++)
    {
        RmNodes in = makeNodes(count);
        uint8_t r[kRmNodesRecSize];
        encodeOk(in, 5 + count, r);

        RmNodes out;
        uint32_t seq = 0;
        TEST_ASSERT_TRUE(rmNodesDecode(r, kRmNodesRecSize, out, seq));
        TEST_ASSERT_EQUAL_UINT32(5 + count, seq);
        TEST_ASSERT_TRUE(rmNodesEqual(in, out));
        for (int i = 0; i < 3; i++)
            TEST_ASSERT_EQUAL(i < count, out.slot[i].used);
    }
}

void test_gap_in_slots_roundtrips(void)
{
    RmNodes in = makeNodes(3);
    in.slot[1].used = false; // delete the middle one
    uint8_t r[kRmNodesRecSize];
    encodeOk(in, 9, r);
    RmNodes out;
    uint32_t seq;
    TEST_ASSERT_TRUE(rmNodesDecode(r, kRmNodesRecSize, out, seq));
    TEST_ASSERT_TRUE(out.slot[0].used);
    TEST_ASSERT_FALSE(out.slot[1].used);
    TEST_ASSERT_TRUE(out.slot[2].used);
    TEST_ASSERT_EQUAL_STRING("DL1XYZ-99", out.slot[2].call);
}

void test_all_unused_record(void)
{
    RmNodes in;
    memset(&in, 0, sizeof(in));
    uint8_t r[kRmNodesRecSize];
    encodeOk(in, 1, r);
    RmNodes out = makeNodes(3);
    uint32_t seq = 0;
    TEST_ASSERT_TRUE(rmNodesDecode(r, kRmNodesRecSize, out, seq));
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_FALSE(out.slot[i].used);
}

void test_unused_slot_garbage_is_not_encoded(void)
{
    RmNodes in = makeNodes(1);
    memset(in.slot[2].call, 'X', RM_NODES_CALL_LEN);   // leftover bytes in an unused slot
    memset(in.slot[2].key, 0xAA, RM_NODES_KEY_LEN);
    in.slot[2].used = false;
    uint8_t r[kRmNodesRecSize];
    encodeOk(in, 2, r);
    for (size_t i = 12 + 86; i < 12 + 129; i++)
        TEST_ASSERT_EQUAL_UINT8(0, r[i]);
    RmNodes out;
    uint32_t seq;
    TEST_ASSERT_TRUE(rmNodesDecode(r, kRmNodesRecSize, out, seq));
    TEST_ASSERT_TRUE(rmNodesEqual(in, out));
}

void test_every_single_bit_flip_is_rejected(void)
{
    RmNodes in = makeNodes(3);
    uint8_t good[kRmNodesRecSize];
    encodeOk(in, 4242, good);

    int rejected = 0;
    for (size_t byte = 0; byte < kRmNodesRecSize; byte++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            uint8_t bad[kRmNodesRecSize];
            memcpy(bad, good, sizeof(bad));
            bad[byte] ^= (uint8_t)(1u << bit);
            RmNodes out;
            uint32_t seq = 99;
            TEST_ASSERT_FALSE_MESSAGE(rmNodesDecode(bad, sizeof(bad), out, seq), "bit flip accepted");
            TEST_ASSERT_EQUAL_UINT32(0, seq);
            rejected++;
        }
    }
    TEST_ASSERT_EQUAL_INT(145 * 8, rejected);
}

void test_wrong_magic_version_nslots_reserved_with_valid_crc(void)
{
    RmNodes in = makeNodes(2);
    uint8_t good[kRmNodesRecSize];
    encodeOk(in, 3, good);

    const size_t offs[] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (size_t o : offs)
    {
        uint8_t bad[kRmNodesRecSize];
        memcpy(bad, good, sizeof(bad));
        bad[o] ^= 0x01;
        refreshCrc(bad); // CRC is fine, so only the field check can reject it
        assertRejected(bad, sizeof(bad));
    }
    // version 0 and 2 explicitly, nslots 2 and 4
    uint8_t bad[kRmNodesRecSize];
    for (uint8_t v : {0, 2, 255})
    {
        memcpy(bad, good, sizeof(bad));
        bad[4] = v;
        refreshCrc(bad);
        assertRejected(bad, sizeof(bad));
    }
    for (uint8_t v : {0, 2, 4})
    {
        memcpy(bad, good, sizeof(bad));
        bad[5] = v;
        refreshCrc(bad);
        assertRejected(bad, sizeof(bad));
    }
}

void test_wrong_length_rejected(void)
{
    RmNodes in = makeNodes(1);
    uint8_t buf[kRmNodesRecSize + 8];
    memset(buf, 0, sizeof(buf));
    encodeOk(in, 3, buf);
    assertRejected(buf, 0);
    assertRejected(buf, 1);
    assertRejected(buf, kRmNodesRecSize - 1);
    assertRejected(buf, kRmNodesRecSize + 1); // trailing zero byte: an over-long file is invalid
    assertRejected(buf, kRmNodesRecSize + 8);
    RmNodes n = makeNodes(3);
    uint32_t seq = 5;
    TEST_ASSERT_FALSE(rmNodesDecode(NULL, kRmNodesRecSize, n, seq));
    TEST_ASSERT_EQUAL_UINT32(0, seq);
}

void test_noncanonical_content_rejected_even_with_valid_crc(void)
{
    RmNodes in = makeNodes(2);
    uint8_t good[kRmNodesRecSize];
    encodeOk(in, 3, good);
    uint8_t bad[kRmNodesRecSize];

    // seq 0
    memcpy(bad, good, sizeof(bad));
    memset(bad + 8, 0, 4);
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // used flag other than 0/1
    memcpy(bad, good, sizeof(bad));
    bad[12 + 42] = 2;
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // unused slot carrying a key byte / a call byte
    memcpy(bad, good, sizeof(bad));
    bad[12 + 86 + 20] = 1;
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));
    memcpy(bad, good, sizeof(bad));
    bad[12 + 86] = 'A';
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // used slot with an empty call
    memcpy(bad, good, sizeof(bad));
    memset(bad + 12, 0, RM_NODES_CALL_LEN);
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // 10 characters, no NUL
    memcpy(bad, good, sizeof(bad));
    memset(bad + 12, 'A', RM_NODES_CALL_LEN);
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // bytes after the NUL
    memcpy(bad, good, sizeof(bad));
    bad[12 + 9] = 'Z';
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // space and control characters in the call
    memcpy(bad, good, sizeof(bad));
    bad[12 + 2] = ' ';
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));
    memcpy(bad, good, sizeof(bad));
    bad[12 + 2] = 0x7F;
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));

    // duplicate call in two used slots
    memcpy(bad, good, sizeof(bad));
    memcpy(bad + 12 + 43, bad + 12, RM_NODES_CALL_LEN);
    refreshCrc(bad);
    assertRejected(bad, sizeof(bad));
}

void test_encode_capacity_and_argument_errors(void)
{
    RmNodes in = makeNodes(3);
    uint8_t buf[kRmNodesRecSize + 4];
    memset(buf, 0xEE, sizeof(buf));

    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(in, 1, buf, 0));
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(in, 1, buf, kRmNodesRecSize - 1));
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(in, 1, NULL, kRmNodesRecSize));
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(in, 0, buf, sizeof(buf))); // seq 0
    for (size_t i = 0; i < sizeof(buf); i++)                              // errors never touch out
        TEST_ASSERT_EQUAL_UINT8(0xEE, buf[i]);

    // exact capacity works and writes nothing beyond 145 bytes
    TEST_ASSERT_EQUAL_UINT32(kRmNodesRecSize, rmNodesEncode(in, 1, buf, kRmNodesRecSize));
    for (size_t i = kRmNodesRecSize; i < sizeof(buf); i++)
        TEST_ASSERT_EQUAL_UINT8(0xEE, buf[i]);

    // invalid call, duplicate call
    RmNodes bad = in;
    bad.slot[1].call[0] = '\0';
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(bad, 1, buf, sizeof(buf)));
    bad = in;
    memset(bad.slot[0].call, 'A', RM_NODES_CALL_LEN); // no NUL
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(bad, 1, buf, sizeof(buf)));
    bad = in;
    memcpy(bad.slot[2].call, bad.slot[0].call, RM_NODES_CALL_LEN);
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesEncode(bad, 1, buf, sizeof(buf)));
    // a duplicate call in an UNUSED slot is not a duplicate
    bad = in;
    memcpy(bad.slot[2].call, bad.slot[0].call, RM_NODES_CALL_LEN);
    bad.slot[2].used = false;
    TEST_ASSERT_EQUAL_UINT32(kRmNodesRecSize, rmNodesEncode(bad, 1, buf, sizeof(buf)));
}

void test_call_and_key_boundary_values(void)
{
    RmNodes in;
    memset(&in, 0, sizeof(in));
    // slot 0: one-character call, all-zero key
    in.slot[0].used = true;
    in.slot[0].call[0] = 'A';
    // slot 1: nine-character call, all-0xFF key
    in.slot[1].used = true;
    memcpy(in.slot[1].call, "DK5EN-123", 9);
    memset(in.slot[1].key, 0xFF, RM_NODES_KEY_LEN);
    // slot 2: printable extremes 0x21 and 0x7E, key with 0x00 inside
    in.slot[2].used = true;
    in.slot[2].call[0] = '!';
    in.slot[2].call[1] = '~';
    in.slot[2].key[5] = 0x00;
    in.slot[2].key[6] = 0x80;

    uint32_t seqs[] = {1, 2, 0x7FFFFFFFUL, 0xFFFFFFFFUL};
    for (uint32_t s : seqs)
    {
        uint8_t r[kRmNodesRecSize];
        encodeOk(in, s, r);
        RmNodes out;
        uint32_t got = 0;
        TEST_ASSERT_TRUE(rmNodesDecode(r, kRmNodesRecSize, out, got));
        TEST_ASSERT_EQUAL_UINT32(s, got);
        TEST_ASSERT_TRUE(rmNodesEqual(in, out));
    }
    // rmNodesCallValid() reads the full 10-byte field, so hand it full fields
    TEST_ASSERT_TRUE(rmNodesCallValid(in.slot[0].call));
    TEST_ASSERT_TRUE(rmNodesCallValid(in.slot[1].call));
    char ten[RM_NODES_CALL_LEN];
    memset(ten, 'A', sizeof(ten));
    TEST_ASSERT_FALSE(rmNodesCallValid(ten)); // 10 characters never fit
}

void test_equal_ignores_unused_leftovers_but_not_content(void)
{
    RmNodes a = makeNodes(2);
    RmNodes b = a;
    memset(b.slot[2].key, 0x55, RM_NODES_KEY_LEN); // unused slot leftover
    TEST_ASSERT_TRUE(rmNodesEqual(a, b));
    b.slot[1].key[31] ^= 1;
    TEST_ASSERT_FALSE(rmNodesEqual(a, b));
    b = a;
    b.slot[0].call[0] = 'X';
    TEST_ASSERT_FALSE(rmNodesEqual(a, b));
    b = a;
    b.slot[2].used = true;
    TEST_ASSERT_FALSE(rmNodesEqual(a, b));
}

void test_scrub_zeroes(void)
{
    RmNodes n = makeNodes(3);
    rmNodesScrub(n);
    RmNodes zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_MEMORY(&zero, &n, sizeof(n));
}

// ---- slot logic ---------------------------------------------------------------------------------

void test_pick_load_save_and_nextseq_table(void)
{
    const RmNodesSlotInfo bad = {false, 0}, s5 = {true, 5}, s6 = {true, 6}, bad9 = {false, 9};

    TEST_ASSERT_EQUAL_INT(-1, rmNodesPickLoad(bad, bad9));
    TEST_ASSERT_EQUAL_INT(0, rmNodesPickLoad(s5, bad));
    TEST_ASSERT_EQUAL_INT(1, rmNodesPickLoad(bad, s5));
    TEST_ASSERT_EQUAL_INT(1, rmNodesPickLoad(s5, s6));
    TEST_ASSERT_EQUAL_INT(0, rmNodesPickLoad(s6, s5));
    TEST_ASSERT_EQUAL_INT(0, rmNodesPickLoad(s5, s5)); // tie: slot 0

    TEST_ASSERT_EQUAL_INT(0, rmNodesPickSave(bad, bad9));
    TEST_ASSERT_EQUAL_INT(1, rmNodesPickSave(s5, bad));  // first save after slot 0 exists: slot 1
    TEST_ASSERT_EQUAL_INT(0, rmNodesPickSave(bad, s5));
    TEST_ASSERT_EQUAL_INT(0, rmNodesPickSave(s5, s6));   // lower seq is overwritten
    TEST_ASSERT_EQUAL_INT(1, rmNodesPickSave(s6, s5));
    TEST_ASSERT_EQUAL_INT(1, rmNodesPickSave(s5, s5));   // tie: the one load() does not take

    // invariant: with a valid slot, the slot save() overwrites is never the one load() takes
    const RmNodesSlotInfo all[] = {bad, s5, s6, {true, 0xFFFFFFFFUL}};
    for (const RmNodesSlotInfo &a : all)
        for (const RmNodesSlotInfo &b : all)
            if (a.valid || b.valid)
                if (a.valid && b.valid)
                    TEST_ASSERT_NOT_EQUAL(rmNodesPickLoad(a, b), rmNodesPickSave(a, b));

    TEST_ASSERT_EQUAL_UINT32(1, rmNodesNextSeq(bad, bad9));
    TEST_ASSERT_EQUAL_UINT32(6, rmNodesNextSeq(s5, bad));
    TEST_ASSERT_EQUAL_UINT32(7, rmNodesNextSeq(s5, s6));
    TEST_ASSERT_EQUAL_UINT32(7, rmNodesNextSeq(s6, s5));
    const RmNodesSlotInfo top = {true, 0xFFFFFFFFUL};
    TEST_ASSERT_EQUAL_UINT32(0, rmNodesNextSeq(top, s5)); // exhausted: refuse
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, rmNodesNextSeq(s5, {true, 0xFFFFFFFEUL}));
}

// ---- fake storage -------------------------------------------------------------------------------

struct FakeStore
{
    std::vector<uint8_t> f[2];
    bool present[2] = {false, false};
    int writes = 0;
    bool failWrite = false;    // write() reports failure and leaves the slot removed
    int tornBytes = -1;        // write() stores only this many bytes but reports success
    bool mangleWrite = false;  // write() flips a bit but reports success

    size_t read(int slot, uint8_t *buf, size_t cap)
    {
        if (!present[slot])
            return 0;
        size_t n = f[slot].size() < cap ? f[slot].size() : cap;
        memcpy(buf, f[slot].data(), n);
        return n;
    }

    bool write(int slot, const uint8_t *buf, size_t len)
    {
        writes++;
        f[slot].clear();
        present[slot] = false; // the real store removes the file first
        if (failWrite)
            return false;
        size_t n = (tornBytes >= 0 && (size_t)tornBytes < len) ? (size_t)tornBytes : len;
        f[slot].assign(buf, buf + n);
        if (mangleWrite && n > 0)
            f[slot][n / 2] ^= 0x04;
        present[slot] = true;
        return true;
    }
};

static uint32_t seqOf(const FakeStore &s, int slot)
{
    RmNodes n;
    uint32_t seq = 0;
    if (!s.present[slot] || !rmNodesDecode(s.f[slot].data(), s.f[slot].size(), n, seq))
        return 0;
    return seq;
}

void test_dual_empty_store_loads_nothing_and_zeroes_output(void)
{
    FakeStore s;
    RmNodes out = makeNodes(3);
    TEST_ASSERT_FALSE(rmNodesDualLoad(s, out));
    RmNodes zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_MEMORY(&zero, &out, sizeof(out));
}

void test_dual_save_alternates_slots_and_load_takes_newest(void)
{
    FakeStore s;
    RmNodes v1 = makeNodes(1), v2 = makeNodes(2), v3 = makeNodes(3);

    TEST_ASSERT_TRUE(rmNodesDualSave(s, v1));
    TEST_ASSERT_EQUAL_UINT32(1, seqOf(s, 0));
    TEST_ASSERT_EQUAL_UINT32(0, seqOf(s, 1));

    TEST_ASSERT_TRUE(rmNodesDualSave(s, v2));
    TEST_ASSERT_EQUAL_UINT32(1, seqOf(s, 0)); // slot A untouched: the previous record survives
    TEST_ASSERT_EQUAL_UINT32(2, seqOf(s, 1));

    TEST_ASSERT_TRUE(rmNodesDualSave(s, v3));
    TEST_ASSERT_EQUAL_UINT32(3, seqOf(s, 0));
    TEST_ASSERT_EQUAL_UINT32(2, seqOf(s, 1));

    RmNodes out;
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(v3, out));

    // the older record is still a complete, loadable previous state
    s.present[0] = false;
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(v2, out));
}

void test_dual_identical_save_skips_the_write(void)
{
    FakeStore s;
    RmNodes v = makeNodes(2);
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v));
    int w = s.writes;
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v));
    TEST_ASSERT_EQUAL_INT(w, s.writes);
    RmNodes changed = v;
    changed.slot[0].key[0] ^= 1;
    TEST_ASSERT_TRUE(rmNodesDualSave(s, changed));
    TEST_ASSERT_EQUAL_INT(w + 1, s.writes);
}

void test_dual_failed_or_torn_save_keeps_previous_state(void)
{
    FakeStore s;
    RmNodes v1 = makeNodes(1), v2 = makeNodes(2);
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v1));
    TEST_ASSERT_TRUE(rmNodesDualSave(s, makeNodes(3))); // slot B = seq 2
    RmNodes before;
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, before));

    // write error: reports false, the older record (the overwritten slot) is gone, newest intact
    s.failWrite = true;
    TEST_ASSERT_FALSE(rmNodesDualSave(s, v2));
    s.failWrite = false;
    RmNodes out;
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(before, out));

    // torn write that the store reports as success: the read-back compare catches it
    for (int torn : {0, 1, 12, 100, 144})
    {
        FakeStore t = s;
        t.tornBytes = torn;
        TEST_ASSERT_FALSE(rmNodesDualSave(t, v2));
        TEST_ASSERT_TRUE(rmNodesDualLoad(t, out)); // the untouched slot still loads
        TEST_ASSERT_TRUE(rmNodesEqual(before, out));
    }

    // a store that silently alters bytes
    FakeStore m = s;
    m.mangleWrite = true;
    TEST_ASSERT_FALSE(rmNodesDualSave(m, v2));
}

void test_dual_corrupt_newest_falls_back_and_both_corrupt_is_empty(void)
{
    FakeStore s;
    RmNodes v1 = makeNodes(1), v2 = makeNodes(2);
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v1)); // A seq1
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v2)); // B seq2

    s.f[1][50] ^= 0x01; // damage the newest
    RmNodes out;
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(v1, out));

    // next save repairs: the invalid slot is the target
    RmNodes v3 = makeNodes(3);
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v3));
    TEST_ASSERT_EQUAL_UINT32(2, seqOf(s, 1)); // seq = max valid (1) + 1
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(v3, out));

    // both dead (the 4 kB page-cache erase case): normal state, load false, output zeroed
    s.f[0][0] ^= 0xFF;
    s.f[1].resize(10);
    out = makeNodes(2);
    TEST_ASSERT_FALSE(rmNodesDualLoad(s, out));
    RmNodes zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_MEMORY(&zero, &out, sizeof(out));
    // and a save from there works
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v1));
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(v1, out));
}

void test_dual_overlong_file_is_invalid(void)
{
    FakeStore s;
    RmNodes v = makeNodes(2);
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v));
    s.f[0].push_back(0); // one stray byte after a valid record
    RmNodes out;
    TEST_ASSERT_FALSE(rmNodesDualLoad(s, out));
    s.f[0].insert(s.f[0].end(), 500, 0); // far longer than the read cap
    TEST_ASSERT_FALSE(rmNodesDualLoad(s, out));
}

void test_dual_seq_exhaustion_refuses_save(void)
{
    FakeStore s;
    RmNodes v1 = makeNodes(1), v2 = makeNodes(2);
    uint8_t r[kRmNodesRecSize];
    encodeOk(v1, 0xFFFFFFFFUL, r);
    s.f[0].assign(r, r + sizeof(r));
    s.present[0] = true;
    RmNodes out;
    TEST_ASSERT_TRUE(rmNodesDualLoad(s, out));
    TEST_ASSERT_FALSE(rmNodesDualSave(s, v2));
    TEST_ASSERT_EQUAL_INT(0, s.writes);
    TEST_ASSERT_TRUE(rmNodesDualSave(s, v1)); // identical content is still fine
}

void test_dual_rejects_unsavable_input(void)
{
    FakeStore s;
    RmNodes bad = makeNodes(2);
    bad.slot[1].call[0] = '\0';
    TEST_ASSERT_FALSE(rmNodesDualSave(s, bad));
    TEST_ASSERT_EQUAL_INT(0, s.writes);
}

void test_blob_save_load_skip_and_corruption(void)
{
    FakeStore s;
    RmNodes out = makeNodes(3);
    TEST_ASSERT_FALSE(rmNodesBlobLoad(s, out));
    RmNodes zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_MEMORY(&zero, &out, sizeof(out));

    RmNodes v1 = makeNodes(1), v2 = makeNodes(2);
    TEST_ASSERT_TRUE(rmNodesBlobSave(s, v1));
    TEST_ASSERT_EQUAL_UINT32(1, seqOf(s, 0));
    TEST_ASSERT_EQUAL_INT(1, s.writes);
    TEST_ASSERT_TRUE(rmNodesBlobSave(s, v1)); // identical: no write
    TEST_ASSERT_EQUAL_INT(1, s.writes);
    TEST_ASSERT_TRUE(rmNodesBlobSave(s, v2));
    TEST_ASSERT_EQUAL_UINT32(2, seqOf(s, 0));
    TEST_ASSERT_TRUE(rmNodesBlobLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(v2, out));

    // forgetting all nodes (all unused) is a real record, not "absent"
    RmNodes none;
    memset(&none, 0, sizeof(none));
    TEST_ASSERT_TRUE(rmNodesBlobSave(s, none));
    TEST_ASSERT_TRUE(rmNodesBlobLoad(s, out));
    TEST_ASSERT_TRUE(rmNodesEqual(none, out));

    // damage, short, long, wrong key size
    s.f[0][20] ^= 0x10;
    TEST_ASSERT_FALSE(rmNodesBlobLoad(s, out));
    s.f[0].resize(7);
    TEST_ASSERT_FALSE(rmNodesBlobLoad(s, out));
    s.present[0] = false;
    TEST_ASSERT_FALSE(rmNodesBlobLoad(s, out));

    // write failure and torn write both report false
    FakeStore f;
    f.failWrite = true;
    TEST_ASSERT_FALSE(rmNodesBlobSave(f, v1));
    FakeStore t;
    t.tornBytes = 60;
    TEST_ASSERT_FALSE(rmNodesBlobSave(t, v1));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_layout_constants_and_golden_offsets);
    RUN_TEST(test_roundtrip_all_slot_counts);
    RUN_TEST(test_gap_in_slots_roundtrips);
    RUN_TEST(test_all_unused_record);
    RUN_TEST(test_unused_slot_garbage_is_not_encoded);
    RUN_TEST(test_every_single_bit_flip_is_rejected);
    RUN_TEST(test_wrong_magic_version_nslots_reserved_with_valid_crc);
    RUN_TEST(test_wrong_length_rejected);
    RUN_TEST(test_noncanonical_content_rejected_even_with_valid_crc);
    RUN_TEST(test_encode_capacity_and_argument_errors);
    RUN_TEST(test_call_and_key_boundary_values);
    RUN_TEST(test_equal_ignores_unused_leftovers_but_not_content);
    RUN_TEST(test_scrub_zeroes);
    RUN_TEST(test_pick_load_save_and_nextseq_table);
    RUN_TEST(test_dual_empty_store_loads_nothing_and_zeroes_output);
    RUN_TEST(test_dual_save_alternates_slots_and_load_takes_newest);
    RUN_TEST(test_dual_identical_save_skips_the_write);
    RUN_TEST(test_dual_failed_or_torn_save_keeps_previous_state);
    RUN_TEST(test_dual_corrupt_newest_falls_back_and_both_corrupt_is_empty);
    RUN_TEST(test_dual_overlong_file_is_invalid);
    RUN_TEST(test_dual_seq_exhaustion_refuses_save);
    RUN_TEST(test_dual_rejects_unsavable_input);
    RUN_TEST(test_blob_save_load_skip_and_corruption);
    return UNITY_END();
}
