// FORMAL-02 wave 4 harness: the phone-write boundary of the ESP32 BLE path (docs/nimble-boundary-20261127.md).
// Real code: NimBLEAttValue.cpp of the patched NimBLE-Arduino 2.2.3 copy (NB-01, NB-02 applied) and bleItemFromWrite()
// of src/ble_session.h. The library is the adversary: malloc/realloc/calloc MAY RETURN NULL here (ESBMC default, no
// --force-malloc-success); for the seam jobs that is the property under test (F1), not an assumption to remove.
// Live value = the characteristic's NimBLEAttValue: boot-time allocation succeeded (assumed), max_len = MAXLEN.
// Modes (-D):
//   MAXLEN=n      max length of the live value (production 300; the lib clamps to 512)
//   WLEN=n        longest write (0..WLEN, nondet; beyond MAXLEN the library refuses it)
//   NOPS=n        number of operations (default 1)
//   MODE_SETAPP   pass: size <= capacity <= max, data[size] == 0, over NOPS nondet setValue/append operations
//   MODE_SEAM     pass: setValue, ONE copy (getValue()), bleItemFromWrite(item, v.data(), v.size()) -> checks below
//   MODE_TODAY    today's onWrite: length from the live value, data from the copy -> NULL source of memcpy (F1)
//   MODE_OOM0     witness: a failed grow leaves length 0 after setValue(len > 0)
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "NimBLEAttValue.h"
#include "attvalue_impl.cpp"   // the real library source (a copy or a mutated copy in gen/, see the .check files)
#include "ble_session.h"

#ifndef MAXLEN
#define MAXLEN 300
#endif
#ifndef WLEN
#define WLEN 301
#endif
#ifndef NOPS
#define NOPS 1
#endif

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

BleStats g_bleStats;
BleQueueCounters g_bleQ;

// exact-size nondet write buffer: any read at or beyond len is out of bounds
static uint8_t *make_write(uint16_t &len)
{
    int n = nondet_int();
    __ESBMC_assume(n >= 0 && n <= WLEN);
    len = (uint16_t)n;
    uint8_t *w = (uint8_t *)malloc(len > 0 ? len : 1);
    __ESBMC_assume(w != NULL);   // the writer's own buffer (the radio buffer) exists; only the library allocations may fail
    for (uint16_t i = 0; i < len; i++)
        w[i] = nondet_uchar();
    return w;
}

static void check_invariants(const NimBLEAttValue &v)
{
    assert(v.data() != nullptr);
    assert(v.size() <= v.capacity());
    assert(v.capacity() <= v.max_size());
    assert(v.data()[v.size()] == 0);
}

int main()
{
    NimBLEAttValue live(20, MAXLEN);   // CONFIG_NIMBLE_CPP_ATT_VALUE_INIT_LENGTH default 20, createCharacteristic(.., 300)
    __ESBMC_assume(live.data() != nullptr);

#if defined(MODE_SETAPP)
    for (int k = 0; k < NOPS; k++)
    {
        uint16_t len;
        uint8_t *w = make_write(len);
        if (nondet_int() & 1)
            live.setValue(w, len);
        else
            live.append(w, len);
        check_invariants(live);
    }
#elif defined(MODE_SEAM)
    uint16_t len;
    uint8_t *w = make_write(len);
    live.setValue(w, len);
    const NimBLEAttValue v = live;   // getValue(): the heap copy; its allocation may fail (data() == NULL, size() == 0)
    static BleQueueItem item;
    bool ok = bleItemFromWrite(item, v.data(), v.size());
    if (ok)
    {
        assert(v.data() != nullptr);
        assert(item.length == v.size() && item.length > 0 && item.length <= MAX_MSG_LEN_PHONE);
        assert(memcmp(item.data, v.data(), item.length) == 0);
    }
    else
    {
        assert(item.length == 0);   // nothing to queue
    }
    if (v.data() == nullptr)
        assert(!ok);                // a failed copy never reaches memcpy and is never queued
    if (live.size() > 0 && v.data() == nullptr)
        bleQueueOnNoCopy(g_bleQ);   // the onWrite accounting condition
#elif defined(MODE_TODAY)
    uint16_t len;
    uint8_t *w = make_write(len);
    live.setValue(w, len);
    static BleQueueItem item;
    item = {};
    item.length = live.length();                           // today: length from the live value ...
    if (item.length > 0 && item.length <= MAX_MSG_LEN_PHONE)
        {
        const NimBLEAttValue cp = live;   // NimBLELocalValueAttribute::getValue() returns by value
        memcpy(item.data, (const uint8_t *)cp, item.length);   // ... data from the heap copy (NULL when its allocation failed)
    }
#elif defined(MODE_OOM0)
    uint16_t len;
    uint8_t *w = make_write(len);
    live.setValue(w, len);
    assert(!(len > 0 && len <= MAXLEN && live.size() == 0));   // OOM0: reachable when the grow fails
#endif
    return 0;
}
