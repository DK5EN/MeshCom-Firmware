// FORMAL-02 wave 6: prepare/execute-write queue of NimBLE 2.2.3 (ble_att_svr.c, prep_free .. rx_exec_write), real code cut
// out by the .check file into gen/<job>/prep_part.inc, compiled unmodified after the environment in stubs/att_stub.h.
// Scenario: NPREP (1..3) prepare-write PDUs, each with handle H_LO..H_HI, offset O_LO..O_HI, value L_LO..L_HI bytes (a
// range with lo == hi is a constant, not a nondet; value bytes = position pattern pat(handle, offset + k), the library
// never branches on the data), then a tail: execute (flags EXEC_FL, or nondet 0/1 with EXEC_FL_ND), or a direct
// ble_att_svr_prep_clear (MODE_CLEAR, the disconnect / timeout path).
// A reference model of the queue (stable sorted by handle, offset; at most MAX_PREP_ENTRIES) is kept beside the code.
// Solver budget: every loop has a constant bound with a guarded body, so --unwind N, N = max(MAXE, LEN, MAX) + 1, is exact
// and the unwinding assertions stay on; the prepare PDUs are unrolled by macro (no loop over NPREP).
#include "att_stub.h"
#include "prep_part.inc"

#ifndef H_LO
#define H_LO 1
#define H_HI 2
#endif
#ifndef O_LO
#define O_LO 0
#define O_HI 1
#endif
#ifndef L_LO
#define L_LO 0
#define L_HI 1
#endif
#ifndef EXEC_FL
#define EXEC_FL 1
#endif
#if L_HI > LEN || H_HI > 2 || H_LO < 1 || NPREP > 3
#error "job bounds outside the model (LEN / two attributes / three PDUs)"
#endif

static int mh[MAXE], mo[MAXE], ml[MAXE], mc;

static int pick(int lo, int hi) {
    if (lo == hi) return lo;
    int v = nondet_int();
    __ESBMC_assume(v >= lo && v <= hi);
    return v;
}
static int list_len(void) {
    int n = 0; struct ble_att_prep_entry *e;
    SLIST_FOREACH(e, &g_conn.bhc_att_svr.basc_prep_list, bape_next) n++;
    return n;
}
static void check_queue_matches_model(void) {
    int i = 0; struct ble_att_prep_entry *e;
    assert(list_len() == mc && mc <= MAXE);
    SLIST_FOREACH(e, &g_conn.bhc_att_svr.basc_prep_list, bape_next) {
        assert(e->bape_handle == mh[i] && e->bape_offset == mo[i] && OS_MBUF_PKTLEN(e->bape_value) == ml[i]);
        i++;
    }
    assert(g_blk_in_use == mc);      /* one pool block per queued entry */
    assert(g_mbuf_live == mc);       /* one value mbuf per queued entry, nothing else alive between PDUs */
}
static void check_all_freed(void) {
    assert(SLIST_EMPTY(&g_conn.bhc_att_svr.basc_prep_list));
    assert(g_blk_in_use == 0);       /* every entry back in the pool */
    assert(g_mbuf_live == 0);        /* every mbuf freed */
}

static void prepare(int h, int o, int l) {
    struct os_mbuf *rx = mb_new(LEAD);
    rx->om_data[0] = (uint8_t)(h & 0xff); rx->om_data[1] = (uint8_t)(h >> 8);
    rx->om_data[2] = (uint8_t)(o & 0xff); rx->om_data[3] = (uint8_t)(o >> 8);
    for (int k = 0; k < LEN; k++) if (k < l) rx->om_data[4 + k] = pat(h, o + k);
    rx->len = (uint16_t)(4 + l);
    g_rsp_calls = 0; g_rsp_rc = -1; g_exp_rsp_len = 1 + 4 + l; g_mf = FAIL_PREP;
    int rc = ble_att_svr_rx_prep_write(1, &rx);
    (void)rc;
    g_mf = 0;
    os_mbuf_free_chain(rx);   /* caller (ble_att_rx) frees what the handler did not take; NULL after a PDU was consumed */
    assert(g_rsp_calls == 1);
    int ok = (g_rsp_rc == 0);
    if (mc == MAXE) {
        assert(!ok && g_rsp_att_err == BLE_ATT_ERR_PREPARE_QUEUE_FULL);   /* full queue answers PREPARE_QUEUE_FULL ... */
    } else if (!FAIL_PREP) {
        assert(ok);
    } else if (!ok) {
        assert(g_rsp_att_err == BLE_ATT_ERR_PREPARE_QUEUE_FULL || g_rsp_att_err == BLE_ATT_ERR_INSUFFICIENT_RES);
    }
#ifdef MODE_WIT_FULL
    assert(!(!ok && g_rsp_att_err == BLE_ATT_ERR_PREPARE_QUEUE_FULL));    /* witness: the queue-full answer is reached */
#endif
    if (ok) {
        int p = mc;   /* insert after every entry with (handle, offset) <= new: stable */
        for (int s = 0; s < MAXE; s++) {
            if (!(p > 0 && (mh[p - 1] > h || (mh[p - 1] == h && mo[p - 1] > o)))) break;
            mh[p] = mh[p - 1]; mo[p] = mo[p - 1]; ml[p] = ml[p - 1]; p--;
        }
        mh[p] = h; mo[p] = o; ml[p] = l; mc++;
        assert(g_conn.bhc_att_svr.basc_prep_timeout_at == (ble_npl_time_t)(g_now + BLE_HS_ATT_SVR_QUEUED_WRITE_TMO));
    }
    check_queue_matches_model();
}

static void exec_write(int flags) {
    struct os_mbuf *rx = mb_new(LEAD);
    rx->om_data[0] = (uint8_t)flags; rx->len = 1;
    /* expected validation result of the model queue */
    int err = 0, errh = 0;
    for (int i = 0; i < MAXE; i++) {
        if (i >= mc) break;
        if (i == 0 || mh[i - 1] != mh[i]) { if (mo[i] != 0) { err = BLE_ATT_ERR_INVALID_OFFSET; errh = mh[i]; break; } }
        else if (mo[i - 1] + ml[i - 1] != mo[i]) { err = BLE_ATT_ERR_INVALID_OFFSET; errh = mh[i]; break; }
        if (mo[i] + ml[i] > BLE_ATT_ATTR_MAX_LEN) { err = BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; errh = mh[i]; break; }
    }
    g_dexp_n = 0;
    if (flags && !err) {
        for (int i = 0; i < MAXE; i++) {
            if (i >= mc) break;
            if (i > 0 && mh[i - 1] == mh[i]) g_dexp_l[g_dexp_n - 1] += ml[i];
            else { g_dexp_h[g_dexp_n] = mh[i]; g_dexp_l[g_dexp_n] = ml[i]; g_dexp_n++; }
        }
    }
    g_dcalls = 0; g_dfailed = 0; g_rsp_calls = 0; g_rsp_rc = -1; g_cmdfail = 0; g_mf = FAIL_EXEC;
    int rc = ble_att_svr_rx_exec_write(1, &rx);
    (void)rc;
    g_mf = 0;
    os_mbuf_free_chain(rx);
    assert(g_rsp_calls == 1);
    if (g_cmdfail) {   /* response buffer failed: INSUFFICIENT_RES, the queue is left alone, nothing delivered */
        assert(g_rsp_rc != 0 && g_rsp_att_err == BLE_ATT_ERR_INSUFFICIENT_RES && g_dcalls == 0);
        check_queue_matches_model();
        ble_att_svr_prep_clear(&g_conn.bhc_att_svr.basc_prep_list);   /* connection teardown */
        mc = 0; check_all_freed();
        return;
    }
    if (flags && err) {
        assert(g_rsp_rc != 0 && g_rsp_att_err == err && g_rsp_err_handle == errh);   /* gap, overlap, over max: rejected ... */
        assert(g_dcalls == 0);                                                       /* ... before any byte is delivered */
#ifdef MODE_WIT_MAXLEN
        assert(err != BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);   /* witness: the over-length rejection is reached */
#endif
    } else if (flags && g_dfailed) {
        assert(g_rsp_rc != 0 && g_rsp_att_err == BLE_ATT_ERR_UNLIKELY && g_rsp_err_handle == g_dfail_handle);
        assert(g_dcalls <= g_dexp_n);
    } else {
        assert(g_rsp_rc == 0);
        assert(g_dcalls == g_dexp_n);   /* flags 0: nothing delivered; flags 1: one delivery per handle, in handle order */
    }
    mc = 0;
    check_all_freed();
}

#define PREPARE_PDU() prepare(pick(H_LO, H_HI), pick(O_LO, O_HI), pick(L_LO, L_HI))

int main() {
    SLIST_INIT(&g_conn.bhc_att_svr.basc_prep_list);
    PREPARE_PDU();
#if NPREP >= 2
    PREPARE_PDU();
#endif
#if NPREP >= 3
    PREPARE_PDU();
#endif
#if defined(MODE_CLEAR)
    ble_att_svr_prep_clear(&g_conn.bhc_att_svr.basc_prep_list);    /* disconnect / timeout path (also the cancel teardown) */
    mc = 0; check_all_freed(); assert(g_dcalls == 0);
#elif defined(EXEC_FL_ND)
    exec_write(pick(0, 1));
#else
    exec_write(EXEC_FL);
#endif
    return 0;
}
