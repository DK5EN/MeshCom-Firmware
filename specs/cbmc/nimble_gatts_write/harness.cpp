// FORMAL-02 wave 4b: GATT server write case of the patched NimBLE-Arduino 2.2.3 (NimBLEServer::handleGattEvent,
// WRITE_DSC/WRITE_CHR). Real code: the case block is cut out of NimBLEServer.cpp by the .check file into
// gen/<job>/write_case.inc (text extraction, no hand copy) and compiled here; the mbuf type is the real os_mbuf.h.
// Stubs: the characteristic (max_size() = MAXLEN, writeEvent records the delivery), the connection info.
// Input: a chain of NCH (1..3) mbufs, each length 0..CHLEN, data in exact-size heap blocks (any read past a block is OOB).
// Modes: MODE_PASS (assert contract), MODE_WIT_FIRST / MODE_WIT_CHAIN (witness: the two INVALID_ATTR_VALUE_LEN exits).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "os/os_mbuf.h"

#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 0x0D
#define BLE_ATT_ERR_UNLIKELY 0x0E
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_DSC 2
struct ble_gatt_access_ctxt { int op; struct os_mbuf *om; };
struct NimBLEConnInfo { int d; };

#ifndef MAXLEN
#define MAXLEN 300
#endif
#ifndef CHLEN
#define CHLEN 320
#endif

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();
extern "C" void __ESBMC_assume(bool);

static int g_calls, g_dlen;
static uint8_t g_exp[3 * CHLEN];
static int g_total;
static uint8_t g_data[3][CHLEN];   // fixed blocks (symbolic-size malloc blew the 6 GB budget)

struct AttVal { uint16_t max_size() const { return MAXLEN; } };
struct LocalAttr {
    AttVal v;
    const AttVal &getAttVal() const { return v; }
    void writeEvent(const uint8_t *buf, uint16_t len, NimBLEConnInfo &) {
        g_calls++; g_dlen = len;
        assert(len <= MAXLEN);                   // contract the app relies on
        __ESBMC_assume(len <= MAXLEN);
        assert(len == g_total);                  // whole chain delivered
        for (int i = 0; i < len; i++) assert(buf[i] == g_exp[i]);   // in order, unchanged
    }
};

static int write_case(uint16_t connHandle, ble_gatt_access_ctxt *ctxt, LocalAttr *pAtt, NimBLEConnInfo &peerInfo) {
    const AttVal &val = pAtt->getAttVal();
    switch (ctxt->op) {
#include "write_case.inc"
        default:
            break;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

int main() {
#ifdef NCH
    const int nch = NCH;   // chain length fixed per job: a symbolic nch makes every next/om_data pointer symbolic
#else
    int nch = nondet_int();
    __ESBMC_assume(nch >= 1 && nch <= 3);
#endif
    struct os_mbuf m[3];
    int tot = 0, first = 0;
    for (int i = 0; i < nch; i++) {
        int l = nondet_int();
        __ESBMC_assume(l >= 0 && l <= CHLEN);
        uint8_t *d = g_data[i];
        // data independence: the write case never branches on the bytes, it only copies them. A fixed pattern that
        // differs per position of the whole chain (tot + k) still catches any lost, duplicated or reordered byte,
        // without a symbolic byte array (symbolic data blew the memory budget even at MAXLEN 3).
        for (int k = 0; k < l; k++) { d[k] = (uint8_t)(1 + tot + k); g_exp[tot + k] = d[k]; }
        m[i].om_data = d; m[i].om_len = l;
        SLIST_NEXT(&m[i], om_next) = (i + 1 < nch) ? &m[i + 1] : NULL;
        if (i == 0) first = l;
        tot += l;
    }
    g_total = tot;
    struct ble_gatt_access_ctxt ctxt;
    ctxt.op = BLE_GATT_ACCESS_OP_WRITE_CHR;   // WRITE_DSC falls through to the same block (case labels stacked)
    ctxt.om = &m[0];
    LocalAttr att; NimBLEConnInfo pi;
    int rc = write_case(1, &ctxt, &att, pi);
#if defined(MODE_PASS)
    if (tot <= MAXLEN) { assert(rc == 0 && g_calls == 1 && g_dlen == tot); }
    else { assert(rc == BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN && g_calls == 0); }   // oversize never delivered
#elif defined(MODE_WIT_FIRST)
    assert(!(rc == BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN && first > MAXLEN));    // reachable -> witness fails
#elif defined(MODE_WIT_CHAIN)
    assert(!(rc == BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN && first <= MAXLEN && tot > MAXLEN));
#endif
    return 0;
}
