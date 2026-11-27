// FORMAL-02 wave 6 (nimble_att_write): environment of the prepare/execute-write queue of NimBLE 2.2.3 ble_att_svr.c.
// The code under proof (ble_att_svr_prep_free .. ble_att_svr_rx_exec_write) is cut out of the library by the .check file
// and compiled unmodified after this header. This header supplies what the cut code references: the library structs
// (copied field for field from ble_att_priv.h / ble_att_cmd_priv.h), constants, and stand-ins for everything outside the
// cut. Model limits (not properties): an mbuf is ONE flat buffer (no chain: os_mbuf_concat copies the bytes of the
// second into the first and frees the second, the byte order and the alloc/free accounting are those of a chain);
// the entry pool is MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES static blocks (the firmware value is 2, platformio.ini;
// the library creates its pool with exactly that count in os_mempool_init, ble_att_svr.c:3202, so the queue bound is
// the pool's, not the cut code's).
// Solver budget (6 GB per job): no malloc (mbufs come from a static pool, never reused: a fixed object graph), no libc
// memset/memcpy (ESBMC models them as unwound loops), every loop of the stubs has a constant bound (guarded body), so
// `--unwind N` with unwinding assertions is exact: N = max(MAXE, LEN, MAX) + 1.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/queue.h>

extern int nondet_int(void);
extern void __ESBMC_assume(_Bool);

#define MYNEWT_VAL(x) MYNEWT_VAL_##x
#ifndef MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES
#define MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES 2
#endif
#define MYNEWT_VAL_BLE_ATT_SVR_QUEUED_WRITE 1
#define MYNEWT_VAL_BLE_GATT_CACHING 0
#define MYNEWT_VAL_BLE_GATT_BLOB_TRANSFER 0
#define MYNEWT_VAL_BLE_HS_DEBUG 0
#ifndef BLE_HS_ATT_SVR_QUEUED_WRITE_TMO
#define BLE_HS_ATT_SVR_QUEUED_WRITE_TMO 5000
#endif
#define BLE_HS_DBG_ASSERT(x) assert(x)
#undef le16toh
#define le16toh(x) (x)   /* little-endian targets */

#ifndef BLE_ATT_ATTR_MAX_LEN
#define BLE_ATT_ATTR_MAX_LEN 512   /* jobs reduce it with -D (position pattern needs a small scope) */
#endif
#define BLE_ATT_ERR_INVALID_OFFSET 0x07
#define BLE_ATT_ERR_PREPARE_QUEUE_FULL 0x09
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 0x0d
#define BLE_ATT_ERR_UNLIKELY 0x0e
#define BLE_ATT_ERR_INSUFFICIENT_RES 0x11
#define BLE_ATT_ERR_INVALID_HANDLE 0x01
#define BLE_ATT_OP_PREP_WRITE_REQ 0x16
#define BLE_ATT_OP_PREP_WRITE_RSP 0x17
#define BLE_ATT_OP_EXEC_WRITE_REQ 0x18
#define BLE_ATT_OP_EXEC_WRITE_RSP 0x19
#define BLE_HS_ENOENT 5
#define BLE_HS_ENOMEM 6
#define BLE_HS_EAPP 14
#define BLE_HS_ENOTSUP 15

typedef uint32_t ble_npl_time_t;

/* ---- job parameters (every job sets them with -D) ---- */
#ifndef NPREP
#define NPREP 2          /* prepare-write PDUs of the scenario */
#endif
#ifndef LEN
#define LEN 2            /* longest value of one prepare-write PDU, bytes */
#endif
#ifndef LEAD
#define LEAD 2           /* leading space of a received mbuf (ATT opcode already stripped by ble_att_rx); 2 keeps om_data
                          * 2-aligned, 1 is the tightest space the response needs (pass_prepend_leading), 0 = today_ job */
#endif
#define MAXE MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES

/* ---- flat mbuf model: a static pool, never reused (fixed object graph, no malloc) ---- */
#define MBUF_CAP (LEAD + 8 + 2 * LEN)     /* rx: LEAD + 4 + LEN; concatenated value: 2 * LEN; asserted at every use */
#define NMB (2 * NPREP + 3)               /* per PDU: rx + one value mbuf; execute: rx; today_ job: + new head */
#if NMB > 9 || MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES > 2
#error "unrolled pool index lookups cover at most 9 mbufs and 2 prep entries"
#endif
struct os_mbuf { uint8_t *om_data; uint16_t len; uint8_t dead; uint8_t buf[MBUF_CAP] __attribute__((aligned(4))); };
#define OS_MBUF_PKTLEN(om) ((om)->len)

/* ---- library structs (ble_att_priv.h:110-127, ble_att_cmd_priv.h:285-300) ---- */
struct ble_att_prep_entry {
    SLIST_ENTRY(ble_att_prep_entry) bape_next;
    uint16_t bape_handle;
    uint16_t bape_offset;
    struct os_mbuf *bape_value;
};
SLIST_HEAD(ble_att_prep_entry_list, ble_att_prep_entry);
struct ble_att_svr_conn {
    struct ble_att_prep_entry_list basc_prep_list;
    ble_npl_time_t basc_prep_timeout_at;
};
struct ble_hs_conn { struct ble_att_svr_conn bhc_att_svr; };
/* The two PDU structs are __attribute__((packed)) in the library (ble_att_cmd_priv.h:285-300): they are overlaid on the
 * received bytes at any address (the ATT opcode strip leaves om_data at an odd address), so a 2-byte member access is
 * unaligned by design. The stub must carry the attribute, or the tool reports a misalignment the real build does not have. */
struct ble_att_prep_write_cmd { uint16_t bapc_handle; uint16_t bapc_offset; } __attribute__((packed));
struct ble_att_exec_write_req { uint8_t baeq_flags; } __attribute__((packed));
struct ble_att_svr_entry { uint16_t ha_handle_id; };
struct os_mempool { int dummy; };

/* ---- observation state ---- */
#ifndef FAIL_PREP
#define FAIL_PREP 0     /* 1: mbuf allocation and appendfrom may fail nondeterministically during prepare-write */
#endif
#ifndef FAIL_EXEC
#define FAIL_EXEC 0     /* 1: the response buffer of execute-write (ble_att_cmd_prepare) may fail */
#endif
#ifndef WFAIL
#define WFAIL 0         /* 1: the attribute write callback may fail nondeterministically */
#endif
static int g_mf;        /* failure injection armed (set by the harness around the call under test) */
static int g_cmdfail;
static struct os_mbuf g_mb[NMB];
static int g_mb_next;
static int g_mbuf_live;
static struct ble_att_prep_entry g_blk[MAXE];
static int g_blk_used[MAXE];
static int g_blk_in_use;
static struct os_mempool ble_att_svr_prep_entry_pool;
static struct ble_hs_conn g_conn;
static struct ble_att_svr_entry g_attr[2] = {{1}, {2}};
static ble_npl_time_t g_now;
static struct os_mbuf *g_prepend_ret;
static int g_rsp_calls, g_rsp_rc, g_rsp_att_err, g_rsp_err_handle, g_exp_rsp_len;
static int g_dcalls, g_dexp_n, g_dexp_h[MAXE], g_dexp_l[MAXE], g_dfailed, g_dfail_handle;

/* The library clears a fresh entry with memset(entry, 0, sizeof *entry), the only memset of the cut code (the debug
 * poison memset is compiled out, BLE_HS_DEBUG 0). ESBMC models libc memset as an unwound loop; a typed store is the
 * same effect on the 12-byte struct without the loop. */
static const struct ble_att_prep_entry g_zero_entry;
static void att_memset(void *p, int c, size_t n) {
    assert(c == 0 && n == sizeof(struct ble_att_prep_entry));
    *(struct ble_att_prep_entry *)p = g_zero_entry;
}
#undef memset
#define memset(p, c, n) att_memset((p), (c), (n))

static inline uint8_t pat(int h, int pos) { return (uint8_t)(h * 37 + pos * 7 + 1); }   /* byte = f(handle, position) */

static struct os_mbuf *mb_new(int lead) {
    assert(g_mb_next < NMB);   /* model limit: NMB covers every job, the pool is never exhausted */
    struct os_mbuf *m = &g_mb[g_mb_next++];
    m->om_data = m->buf + lead; m->len = 0; m->dead = 0; g_mbuf_live++;
    return m;
}
static void os_mbuf_free_chain(struct os_mbuf *om) {
    if (om == NULL) return;
    /* index by pointer equality, not by pointer difference: om - g_mb divides by a non-power-of-two
     * struct size, which the bit-vector solver cannot handle (the jobs ran out of memory or time) */
    int idx = -1;   /* unrolled with constant indices: no loop, so the jobs' --unwind 3 stays exact */
#define MB_IDX(i) if ((i) < NMB && om == &g_mb[(i) < NMB ? (i) : 0]) idx = (i);
    MB_IDX(0) MB_IDX(1) MB_IDX(2) MB_IDX(3) MB_IDX(4) MB_IDX(5) MB_IDX(6) MB_IDX(7) MB_IDX(8)
#undef MB_IDX
    assert(idx >= 0 && idx < g_mb_next);
    assert(!om->dead);                       /* no double free */
    assert(g_mbuf_live > 0);
    g_mbuf_live--;
    om->dead = 1; om->om_data = NULL; om->len = 0xffff;   /* poison: a later use dereferences NULL */
}
static struct os_mbuf *ble_hs_mbuf_l2cap_pkt(void) {
    if (g_mf && nondet_int()) return NULL;
    return mb_new(0);
}
static int os_mbuf_appendfrom(struct os_mbuf *dst, const struct os_mbuf *src, uint16_t off, uint16_t n) {
    if (g_mf && nondet_int()) return BLE_HS_ENOMEM;
    assert((int)off + (int)n <= src->len && n <= LEN);   /* LEN: model limit of the job */
    assert((dst->om_data - dst->buf) + dst->len + n <= MBUF_CAP);
    for (int i = 0; i < LEN; i++) if (i < n) dst->om_data[dst->len + i] = src->om_data[off + i];
    dst->len = (uint16_t)(dst->len + n);
    return 0;
}
static void os_mbuf_concat(struct os_mbuf *first, struct os_mbuf *second) {
    assert(first != NULL && second != NULL && first != second && second->len <= LEN);
    assert((first->om_data - first->buf) + first->len + second->len <= MBUF_CAP);
    for (int i = 0; i < LEN; i++) if (i < second->len) first->om_data[first->len + i] = second->om_data[i];
    first->len = (uint16_t)(first->len + second->len);
    os_mbuf_free_chain(second);   /* chain ownership moved: accounted as one free */
}
static void os_mbuf_adj(struct os_mbuf *om, int n) {
    assert(n == om->len);   /* the cut code only clears the whole packet */
    om->len = 0;
}
static struct os_mbuf *os_mbuf_prepend(struct os_mbuf *om, int n) {
#if LEAD >= 1
    /* leading space available (the premise of pass_prepend_leading): the old head stays the head */
    assert(om->om_data - om->buf >= n);
    om->om_data -= n; om->len = (uint16_t)(om->len + n);
    g_prepend_ret = om;
    return om;
#else
    /* no leading space: the library puts a NEW head in front; the old pointer is no longer the head. Only the new head
     * is modelled (n bytes, the chain link to the old mbuf is not): enough for the today_ job, which stops at the
     * first use of the stale pointer. */
    struct os_mbuf *nm = mb_new(0);
    nm->len = (uint16_t)n;
    g_prepend_ret = nm;
    return nm;
#endif
}
static void *ble_att_cmd_prepare(uint8_t op, size_t len, struct os_mbuf *txom) {
    assert(len == 0 && txom->len == 0);
    if (g_mf && nondet_int()) { g_cmdfail = 1; return NULL; }
    txom->om_data[0] = op; txom->len = 1;
    return txom->om_data;
}
static void *os_memblock_get(struct os_mempool *p) {
    (void)p;
    for (int i = 0; i < MAXE; i++)   /* the pool: MAXE blocks, as os_mempool_init(..., MAX_PREP_ENTRIES, ...) at ble_att_svr.c:3202 */
        if (!g_blk_used[i]) { g_blk_used[i] = 1; g_blk_in_use++; return &g_blk[i]; }
    return NULL;
}
static int os_memblock_put(struct os_mempool *p, void *b) {
    (void)p;
    int idx = -1;   /* pointer equality, not difference (see os_mbuf_free_chain); unrolled, MAXE <= 2 */
    if ((struct ble_att_prep_entry *)b == &g_blk[0]) idx = 0;
    if (MAXE > 1 && (struct ble_att_prep_entry *)b == &g_blk[MAXE > 1 ? 1 : 0]) idx = 1;
    assert(idx >= 0 && idx < MAXE);   /* block from the pool */
    assert(g_blk_used[idx]);          /* no double put */
    g_blk_used[idx] = 0; g_blk_in_use--;
    /* poison: a later read of the entry dereferences an invalid pointer */
    g_blk[idx] = g_zero_entry;
    g_blk[idx].bape_handle = 0xffff; g_blk[idx].bape_offset = 0xffff;
    g_blk[idx].bape_value = (struct os_mbuf *)(uintptr_t)1;
    SLIST_NEXT(&g_blk[idx], bape_next) = (struct ble_att_prep_entry *)(uintptr_t)1;
    return 0;
}
static void ble_hs_lock(void) {}
static void ble_hs_unlock(void) {}
static void ble_hs_timer_resched(void) {}
static ble_npl_time_t ble_npl_time_get(void) { g_now = (ble_npl_time_t)nondet_int(); return g_now; }
static struct ble_hs_conn *ble_hs_conn_find_assert(uint16_t h) { (void)h; return &g_conn; }
static int ble_att_svr_pullup_req_base(struct os_mbuf **om, int base_len, uint8_t *err) {
    if ((*om)->len < base_len) { *err = 0x04; return BLE_HS_ENOMEM; }
    return 0;
}
static struct ble_att_svr_entry *ble_att_svr_find_by_handle(uint16_t h) {
    return (h == 1 || h == 2) ? &g_attr[h - 1] : NULL;
}
static int ble_att_svr_check_perms(uint16_t c, int is_read, struct ble_att_svr_entry *e, uint8_t *err) {
    (void)c; (void)is_read; (void)e; (void)err; return 0;
}
/* application write: delivery must be exactly the next expected group, bytes = position pattern of the attribute */
static int ble_att_svr_write(uint16_t c, struct ble_att_svr_entry *e, uint16_t off, struct os_mbuf **om, uint8_t *err) {
    (void)c;
    assert(off == 0 && om != NULL && *om != NULL);
    assert((*om)->len <= BLE_ATT_ATTR_MAX_LEN);   /* what the application relies on: never more than the attribute maximum */
    assert(g_dcalls < g_dexp_n);
    assert(e->ha_handle_id == g_dexp_h[g_dcalls]);
    assert((*om)->len == g_dexp_l[g_dcalls]);
    for (int i = 0; i < BLE_ATT_ATTR_MAX_LEN; i++) if (i < (*om)->len) assert((*om)->om_data[i] == pat(e->ha_handle_id, i));
    g_dcalls++;
    if (WFAIL && nondet_int()) { g_dfailed = 1; g_dfail_handle = e->ha_handle_id; *err = BLE_ATT_ERR_UNLIKELY; return BLE_HS_EAPP; }
    return 0;
}
static int ble_att_svr_tx_rsp(uint16_t c, int hs_status, struct os_mbuf *om, uint8_t req_op, uint8_t att_err, uint16_t err_handle) {
    (void)c;
    g_rsp_calls++; g_rsp_rc = hs_status;
    if (hs_status == 0) {
        assert(om != NULL);
        if (req_op == BLE_ATT_OP_PREP_WRITE_REQ) {
            assert(om == g_prepend_ret);                    /* response goes out on the real head */
            assert(om->om_data[0] == BLE_ATT_OP_PREP_WRITE_RSP);
            assert(om->len == g_exp_rsp_len);
        } else {
            assert(om->om_data[0] == BLE_ATT_OP_EXEC_WRITE_RSP && om->len == 1);
        }
    } else {
        g_rsp_att_err = att_err; g_rsp_err_handle = err_handle;
    }
    os_mbuf_free_chain(om);
    return 0;
}
