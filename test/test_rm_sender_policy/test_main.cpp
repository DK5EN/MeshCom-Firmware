// RM web GUI, contract C2: src/rm_validate.h and src/rm_sender_policy.h (header-only, no Arduino).
//
//   pio test -e native_rm_sender_policy
//
// Covers the password and call validators (accept/reject tables), the per-target send policy
// (10 s spacing, 2 unanswered sends in 120 s (10 proven), boundaries, millis() wrap), the entry state
// classification, the plain-English sentences for every RM error token, and the compact status
// token (`s=<letters> p=<cur>/<max>`) incl. the worst-case reply length against RM_MAX_RESULT.

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <rm_sender_policy.h>
#include <rm_validate.h>

#define RM_MAX_RESULT_REPLY 63  // remote_cmd.h RM_MAX_RESULT; kept literal: this env builds no src/

void setUp(void) {}
void tearDown(void) {}

// ---- password validator --------------------------------------------------------------------------------

struct PwCase
{
    const char *pw;
    RmPasswdProblem want;
};

static void test_password_table(void)
{
    const PwCase t[] = {
        {"a", RM_PW_OK},
        {"secret", RM_PW_OK},
        {"p@ss w0rd!", RM_PW_OK},        // inner space is fine
        {"abcdefghijklmn", RM_PW_OK},    // 14 bytes
        {"~!@#$%^&*()_+", RM_PW_OK},
        {"None", RM_PW_OK},              // only the exact word "none" clears
        {"none1", RM_PW_OK},
        {"nonee", RM_PW_OK},
        {"", RM_PW_EMPTY},
        {"abcdefghijklmno", RM_PW_TOO_LONG},  // 15 bytes
        {" x", RM_PW_LEADING_SPACE},          // the M-1 regression: set in the status, dead in the RM hook
        {"  x", RM_PW_LEADING_SPACE},
        {" ", RM_PW_LEADING_SPACE},
        {"x ", RM_PW_TRAILING_SPACE},
        {"x  ", RM_PW_TRAILING_SPACE},
        {" x ", RM_PW_LEADING_SPACE},
        {"none", RM_PW_RESERVED},
        {"a\tb", RM_PW_BAD_CHAR},
        {"a\nb", RM_PW_BAD_CHAR},
        {"a\r", RM_PW_BAD_CHAR},
        {"a\x01" "b", RM_PW_BAD_CHAR},
        {"a\x7f" "b", RM_PW_BAD_CHAR},        // DEL
        {"gr\xc3\xbc\xc3\x9f" "e", RM_PW_BAD_CHAR},  // "grüße" in UTF-8
        {"\xe2\x82\xac", RM_PW_BAD_CHAR},     // euro sign
        {"\xff", RM_PW_BAD_CHAR},
        {"\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc", RM_PW_TOO_LONG},  // 16 bytes UTF-8
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "case %u", (unsigned)i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(t[i].want, rmPasswordProblem(t[i].pw), msg);
        TEST_ASSERT_EQUAL_MESSAGE(t[i].want == RM_PW_OK, rmValidatePassword(t[i].pw), msg);
    }
    TEST_ASSERT_EQUAL_INT(RM_PW_EMPTY, rmPasswordProblem(nullptr));
    TEST_ASSERT_FALSE(rmValidatePassword(nullptr));
}

// Regression: a password with a leading space shows "password set" (all-spaces test only) but RM ignores it
// (the receive hook treats node_passwd[0] == ' ' as "no password"). The validator must reject it, and a
// space-padded node_passwd of a VALID password must still be accepted once trimmed by the caller.
static void test_leading_space_password_is_rejected(void)
{
    TEST_ASSERT_FALSE(rmValidatePassword(" x"));
    TEST_ASSERT_FALSE(rmValidatePassword("  hunter2"));
    TEST_ASSERT_FALSE(rmValidatePassword(" "));
    TEST_ASSERT_EQUAL_INT(RM_PW_LEADING_SPACE, rmPasswordProblem(" x"));
    TEST_ASSERT_TRUE(rmValidatePassword("x"));
    // the length-explicit variant (sender: trailing spaces already stripped by the caller)
    TEST_ASSERT_TRUE(rmValidatePasswordN("abc   ", 3));
    TEST_ASSERT_FALSE(rmValidatePasswordN(" abc", 4));
    TEST_ASSERT_FALSE(rmValidatePasswordN("abc", 0));
    TEST_ASSERT_FALSE(rmValidatePasswordN(nullptr, 0));
}

static void test_password_never_truncates(void)
{
    // 14 passes, 15 does not, whatever the 15th byte is: reject, never cut
    TEST_ASSERT_TRUE(rmValidatePassword("12345678901234"));
    TEST_ASSERT_FALSE(rmValidatePassword("123456789012345"));
    TEST_ASSERT_FALSE(rmValidatePasswordN("12345678901234 ", 15));
}

static void test_every_password_problem_has_a_sentence(void)
{
    for (int p = RM_PW_OK; p <= RM_PW_RESERVED; p++)
    {
        const char *m = rmPasswordProblemText((RmPasswdProblem)p);
        TEST_ASSERT_NOT_NULL(m);
        TEST_ASSERT_TRUE(strlen(m) > 5);
    }
}

// ---- call validator ------------------------------------------------------------------------------------

static void test_call_table(void)
{
    const char *ok[] = {"DK5EN-1", "DK5EN-12", "OE1ABC-15", "DB0ABC-99", "A1-1", "AB-0", "OE3XYZ-4",
                        "DL1ABCD-1" /* 9 */, "9A1B-2"};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(rmValidateCall(ok[i]), ok[i]);

    const char *bad[] = {
        "",                    // empty
        "DK5EN",               // no SSID
        "DK5EN-",              // empty SSID
        "-1",                  // empty base
        "A-1",                 // base too short
        "DK5EN-123",           // SSID 3 digits
        "DK5EN-1A",            // SSID not numeric
        "DK5EN--1",            // double dash
        "DK-5EN-1",            // dash inside the base
        "dk5en-1",             // lower case: the caller folds first
        "Dk5en-1",
        "DL1ABCDE-1",          // 10 chars
        "DL1ABCDEFG-1",
        "DK5EN-1 ",            // trailing space
        " DK5EN-1",
        "DK5EN-1\n",
        "DK 5EN-1",
        "DK5EN/P-1",           // '/' is not routable here
        "DK5\xc3\x9c-1",       // UTF-8
        "DK5EN-\xd9\xa1",      // non-ASCII digit
        "DK5EN:1",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(rmValidateCall(bad[i]), bad[i]);
    TEST_ASSERT_FALSE(rmValidateCall(nullptr));
}

// ---- entry state -----------------------------------------------------------------------------------------

static RmPolEntry mk(uint32_t sentMs, bool replied = false, bool verified = false, bool err = false,
                     bool expired = false)
{
    RmPolEntry e;
    e.sentMs = sentMs;
    e.replied = replied;
    e.verified = verified;
    e.replyErr = err;
    e.expired = expired;
    return e;
}

static void test_state_classification_and_boundaries(void)
{
    const uint32_t t0 = 1000000;
    TEST_ASSERT_EQUAL_INT(RM_ST_QUEUED, rmPolicyState(mk(t0), t0));
    TEST_ASSERT_EQUAL_INT(RM_ST_QUEUED, rmPolicyState(mk(t0), t0 + RM_QUEUED_MS - 1));
    TEST_ASSERT_EQUAL_INT(RM_ST_WAITING, rmPolicyState(mk(t0), t0 + RM_QUEUED_MS));
    TEST_ASSERT_EQUAL_INT(RM_ST_WAITING, rmPolicyState(mk(t0), t0 + 74999));
    TEST_ASSERT_EQUAL_INT(RM_ST_NOANSWER, rmPolicyState(mk(t0), t0 + 75000));
    TEST_ASSERT_EQUAL_INT(RM_ST_NOANSWER, rmPolicyState(mk(t0), t0 + 600000));
    TEST_ASSERT_EQUAL_INT(RM_ST_OK, rmPolicyState(mk(t0, true, true, false), t0 + 5000));
    TEST_ASSERT_EQUAL_INT(RM_ST_ERR, rmPolicyState(mk(t0, true, true, true), t0 + 5000));
    // a reply that did not verify is never green, at any age
    TEST_ASSERT_EQUAL_INT(RM_ST_UNVERIFIED, rmPolicyState(mk(t0, true, false), t0 + 1000));
    TEST_ASSERT_EQUAL_INT(RM_ST_UNVERIFIED, rmPolicyState(mk(t0, true, false), t0 + 900000));
    // an answered entry stays answered after it aged out
    TEST_ASSERT_EQUAL_INT(RM_ST_OK, rmPolicyState(mk(t0, true, true, false, true), t0 + 900000));
    // expired and unanswered = no answer, even when a millis() wrap makes the age look young
    TEST_ASSERT_EQUAL_INT(RM_ST_NOANSWER, rmPolicyState(mk(t0, false, false, false, true), t0 + 10));
}

static void test_state_across_millis_wrap(void)
{
    const uint32_t sent = 0xFFFFFFF0u;           // 16 ms before the wrap
    TEST_ASSERT_EQUAL_INT(RM_ST_QUEUED, rmPolicyState(mk(sent), 5));            // age 21 ms
    TEST_ASSERT_EQUAL_INT(RM_ST_WAITING, rmPolicyState(mk(sent), 10000));       // age ~10 s
    TEST_ASSERT_EQUAL_INT(RM_ST_WAITING, rmPolicyState(mk(sent), 74983));       // 74999 ms
    TEST_ASSERT_EQUAL_INT(RM_ST_NOANSWER, rmPolicyState(mk(sent), 74984));      // 75000 ms
}

static void test_state_names_and_messages(void)
{
    const char *names[] = {"queued", "waiting", "noanswer", "ok", "err", "unverified"};
    for (int s = RM_ST_QUEUED; s <= RM_ST_UNVERIFIED; s++)
    {
        TEST_ASSERT_EQUAL_STRING(names[s], rmStateName((RmEntryState)s));
        const char *m = rmStateMessage((RmEntryState)s);
        TEST_ASSERT_NOT_NULL(m);
        TEST_ASSERT_TRUE(strlen(m) > 10);
    }
    // err entries carry the sentence of the reply's token
    TEST_ASSERT_EQUAL_STRING(rmErrTokenMessage("failed"), rmEntryMessage(RM_ST_ERR, "err failed"));
    TEST_ASSERT_EQUAL_STRING(rmErrTokenMessage("not output"), rmEntryMessage(RM_ST_ERR, "err not output"));
    TEST_ASSERT_EQUAL_STRING(rmStateMessage(RM_ST_OK), rmEntryMessage(RM_ST_OK, "ok gps=on"));
    TEST_ASSERT_TRUE(rmReplyIsErr("err failed"));
    TEST_ASSERT_TRUE(rmReplyIsErr("err"));
    TEST_ASSERT_FALSE(rmReplyIsErr("error"));
    TEST_ASSERT_FALSE(rmReplyIsErr("ok err"));
    TEST_ASSERT_FALSE(rmReplyIsErr(nullptr));
}

// ---- error token sentences -------------------------------------------------------------------------------

static void test_every_error_token_has_a_sentence(void)
{
    // the RM error and verdict tokens of the contract, plus what the sender itself refuses with
    const char *tokens[] = {"failed", "not output", "unsupported", "storage", "blocked", "rate", "lockout",
                            "replay", "tag", "sync", "format", "disabled", "cached",
                            "limit", "busy", "passwd", "dst", "cmd", "ctr", "store", "send", "size", "short", "form"};
    const char *generic = rmErrTokenMessage("no such token at all");
    for (size_t i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++)
    {
        const char *m = rmErrTokenMessage(tokens[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(m, tokens[i]);
        TEST_ASSERT_TRUE_MESSAGE(strlen(m) > 10, tokens[i]);
        TEST_ASSERT_TRUE_MESSAGE(strcmp(m, generic) != 0, tokens[i]);  // a real sentence, not the fallback
        // short plain sentences for a non-technical operator: no command-line vocabulary
        TEST_ASSERT_TRUE_MESSAGE(strlen(m) < 140, tokens[i]);
        TEST_ASSERT_NULL_MESSAGE(strstr(m, "--"), tokens[i]);
        // also reachable through the "err <token>" reply form
        char reply[40];
        snprintf(reply, sizeof(reply), "err %s", tokens[i]);
        TEST_ASSERT_EQUAL_STRING(m, rmErrTokenMessage(reply));
    }
    // the table itself: no empty entry, no duplicate token
    size_t n = 0;
    const RmTokenMsg *tab = rmTokenTable(&n);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_TRUE(strlen(tab[i].token) > 0 && strlen(tab[i].msg) > 10);
        for (size_t j = i + 1; j < n; j++)
            TEST_ASSERT_TRUE(strcmp(tab[i].token, tab[j].token) != 0);
    }
    // unknown, null and an empty token never return an empty string
    TEST_ASSERT_TRUE(strlen(rmErrTokenMessage(nullptr)) > 0);
    TEST_ASSERT_TRUE(strlen(rmErrTokenMessage("")) > 0);
    TEST_ASSERT_TRUE(strlen(rmErrTokenMessage("err ")) > 0);
    // a token must match as a whole word: "failedx" is not "failed", "tag" is not "tagged"
    TEST_ASSERT_EQUAL_STRING(generic, rmErrTokenMessage("failedx"));
    TEST_ASSERT_EQUAL_STRING(generic, rmErrTokenMessage("tagged"));
}

// ---- send policy -----------------------------------------------------------------------------------------

static void test_policy_free_target_may_send(void)
{
    const RmPolDecision d = rmPolicyMaySend(nullptr, 0, 5000);
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_OK, d.reason);
    TEST_ASSERT_EQUAL_UINT32(0, d.retryS);
    TEST_ASSERT_TRUE(rmPolicyMaySend(nullptr, 0, 0).allowed);
}

static void test_policy_cooldown_boundary(void)
{
    const uint32_t t0 = 500000;
    const RmPolEntry e[1] = {mk(t0, true, true)};  // answered: only the spacing applies
    RmPolDecision d = rmPolicyMaySend(e, 1, t0);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_COOLDOWN, d.reason);
    TEST_ASSERT_EQUAL_UINT32(10, d.retryS);
    d = rmPolicyMaySend(e, 1, t0 + 9999);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_UINT32(1, d.retryS);                    // 1 ms left rounds up to 1 s
    d = rmPolicyMaySend(e, 1, t0 + 9000);
    TEST_ASSERT_EQUAL_UINT32(1, d.retryS);                    // exactly 1000 ms left
    d = rmPolicyMaySend(e, 1, t0 + 8999);
    TEST_ASSERT_EQUAL_UINT32(2, d.retryS);                    // 1001 ms left
    d = rmPolicyMaySend(e, 1, t0 + 10000);                    // boundary: out of the cooldown
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_OK, d.reason);
    TEST_ASSERT_EQUAL_UINT32(0, d.retryS);
}

static void test_policy_two_unanswered_inside_90s_lock_the_third(void)
{
    const uint32_t now = 1000000;
    // one unanswered send, 20 s old: allowed (second try)
    RmPolEntry one[1] = {mk(now - 20000)};
    TEST_ASSERT_TRUE(rmPolicyMaySend(one, 1, now).allowed);
    TEST_ASSERT_EQUAL_UINT8(1, rmPolicyMaySend(one, 1, now).unanswered);

    // two unanswered, 30 s and 15 s old: the third is refused until the OLDER one leaves the window
    RmPolEntry two[2] = {mk(now - 15000), mk(now - 30000)};
    RmPolDecision d = rmPolicyMaySend(two, 2, now);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, d.reason);
    TEST_ASSERT_EQUAL_UINT8(2, d.unanswered);
    TEST_ASSERT_EQUAL_UINT32(120, d.retryS);  // 150 s - 30 s

    // order of the entries must not matter
    RmPolEntry two_r[2] = {mk(now - 30000), mk(now - 15000)};
    d = rmPolicyMaySend(two_r, 2, now);
    TEST_ASSERT_EQUAL_UINT32(120, d.retryS);

    // boundary: the older one is exactly 150 s old -> out of the window -> one unanswered left -> allowed
    d = rmPolicyMaySend(two, 2, now + 120000);
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_EQUAL_UINT8(1, d.unanswered);
    // 1 ms before: still locked, 1 s to wait
    d = rmPolicyMaySend(two, 2, now + 119999);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, d.reason);
    TEST_ASSERT_EQUAL_UINT32(1, d.retryS);
}

static void test_policy_three_and_more_unanswered_wait_for_enough_to_leave(void)
{
    const uint32_t now = 2000000;
    // ages 80 s, 50 s, 20 s (all unanswered, e.g. the policy was bypassed by an older firmware)
    RmPolEntry e[3] = {mk(now - 20000), mk(now - 80000), mk(now - 50000)};
    RmPolDecision d = rmPolicyMaySend(e, 3, now);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_UINT8(3, d.unanswered);
    // two must leave (80 s and 50 s) before only one is left: the 50 s one leaves after 100 s
    TEST_ASSERT_EQUAL_UINT32(100, d.retryS);
    TEST_ASSERT_TRUE(rmPolicyMaySend(e, 3, now + 100000).allowed);
    TEST_ASSERT_FALSE(rmPolicyMaySend(e, 3, now + 99999).allowed);
}

static void test_policy_answered_and_aged_out_do_not_count(void)
{
    const uint32_t now = 3000000;
    // verified ok / verified err are answered; 150 s old and expired ones left the window
    RmPolEntry e[4] = {mk(now - 20000, true, true, false), mk(now - 25000, true, true, true),
                       mk(now - 150000), mk(now - 200000, false, false, false, true)};
    RmPolDecision d = rmPolicyMaySend(e, 4, now);
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_EQUAL_UINT8(0, d.unanswered);
    // an unverified reply is NOT an answer (it could be forged): it still counts
    RmPolEntry u[2] = {mk(now - 30000, true, false), mk(now - 20000)};
    d = rmPolicyMaySend(u, 2, now);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, d.reason);
    // 149999 ms old still counts, 150000 does not
    RmPolEntry b[2] = {mk(now - 149999), mk(now - 5000)};
    TEST_ASSERT_FALSE(rmPolicyMaySend(b, 2, now).allowed);
    RmPolEntry c[2] = {mk(now - 150000), mk(now - 5000)};
    d = rmPolicyMaySend(c, 2, now);
    TEST_ASSERT_EQUAL_UINT8(1, d.unanswered);               // only the 5 s one counts
    TEST_ASSERT_EQUAL_INT(RM_POL_COOLDOWN, d.reason);       // ... and it is inside the spacing, not the limit
}

static void test_policy_cooldown_and_limit_combine_to_the_longer_wait(void)
{
    const uint32_t now = 4000000;
    // two unanswered, newest 2 s old (cooldown 8 s left), oldest 145 s old (limit frees in 5 s)
    RmPolEntry e[2] = {mk(now - 2000), mk(now - 145000)};
    RmPolDecision d = rmPolicyMaySend(e, 2, now);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_UINT32(8, d.retryS);   // the spacing is the longer wait
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, d.reason);
    // answered newest send 2 s ago + one unanswered old send: spacing only
    RmPolEntry f[2] = {mk(now - 2000, true, true), mk(now - 85000)};
    d = rmPolicyMaySend(f, 2, now);
    TEST_ASSERT_EQUAL_INT(RM_POL_COOLDOWN, d.reason);
    TEST_ASSERT_EQUAL_UINT32(8, d.retryS);
}

static void test_policy_across_millis_wrap(void)
{
    // sends shortly before the 2^32 wrap, the check shortly after it: ages are unsigned differences
    const uint32_t s1 = 0xFFFFFFFFu - 20000u;   // 20 s before the wrap (ms 4294947295)
    const uint32_t s2 = 0xFFFFFFFFu - 5000u;    // 5 s before
    RmPolEntry e[2] = {mk(s2), mk(s1)};
    // now = 10 s after the wrap: ages 15001 and 30001 ms
    const uint32_t now = 10000u - 1u;           // age(s2) = 5000 + 10000 = 15000 ms
    RmPolDecision d = rmPolicyMaySend(e, 2, now);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, d.reason);
    // oldest = s1 at age 30000 (s1 + 30000 wraps to 9999): 120 s to go
    TEST_ASSERT_EQUAL_UINT32(120, d.retryS);
    // 120 s later (now + 120000, wrapped past 0 again nowhere) the oldest left the window
    TEST_ASSERT_TRUE(rmPolicyMaySend(e, 2, now + 120000).allowed);

    // cooldown boundary exactly over the wrap
    RmPolEntry w[1] = {mk(0xFFFFFFFFu - 4999u, true, true)};   // sent 5000 ms before the wrap point
    TEST_ASSERT_FALSE(rmPolicyMaySend(w, 1, 4999).allowed);     // age 9999
    TEST_ASSERT_TRUE(rmPolicyMaySend(w, 1, 5000).allowed);      // age 10000

    // an expired flag guards the one case unsigned math can not: an entry 49.7 days old looks young again
    RmPolEntry old[2] = {mk(1000, false, false, false, true), mk(2000, false, false, false, true)};
    TEST_ASSERT_TRUE(rmPolicyMaySend(old, 2, 3000u).allowed);   // expired: out, whatever the age looks like
    // (the same entries without the flag would be 1 ms apart and "young": limit)
    RmPolEntry young[2] = {mk(1000), mk(2000)};
    TEST_ASSERT_FALSE(rmPolicyMaySend(young, 2, 3000).allowed);
}

static void test_policy_entry_cap_is_safe(void)
{
    RmPolEntry many[20];
    for (int i = 0; i < 20; i++)
        many[i] = mk(1000000u - (uint32_t)i * 1000u);
    const RmPolDecision d = rmPolicyMaySend(many, 20, 1000000u);  // must not overrun the internal array
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_TRUE(d.unanswered <= RM_POLICY_MAX_ENTRIES);
}

static void test_auto_sync_is_conditional(void)
{
    TEST_ASSERT_TRUE(rmPolicyNeedSync(false, false, false));   // no clock, no mark: sync first
    TEST_ASSERT_FALSE(rmPolicyNeedSync(true, false, false));   // trusted clock: ctr = time is enough
    TEST_ASSERT_FALSE(rmPolicyNeedSync(false, true, false));   // learnt mark: ctr = mark + 1
    TEST_ASSERT_FALSE(rmPolicyNeedSync(true, true, false));
    TEST_ASSERT_FALSE(rmPolicyNeedSync(false, false, true));   // the sync itself never triggers a sync
}

// ---- chain of an automatic sync ---------------------------------------------------------------------------

// rmPendingDecide(syncVerified, syncReplied, syncExpired, syncGone, ageMs, sinceVerifiedMs)
static void test_pending_decision_table(void)
{
    // no reply yet: wait until 75 s, then drop
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(false, false, false, false, 0, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(false, false, false, false, 74999, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, false, false, false, 75000, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, false, false, false, 400000, 0));
    // verified: send only 10.5 s after the verified reply
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(true, true, false, false, 4000, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(true, true, false, false, 14000, 10499));
    TEST_ASSERT_EQUAL_INT(RM_PEND_SEND, rmPendingDecide(true, true, false, false, 14500, 10500));
    TEST_ASSERT_EQUAL_INT(RM_PEND_SEND, rmPendingDecide(true, true, false, false, 90000, 60000));
    // gone or expired: drop, whatever else is true
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, false, false, true, 1000, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(true, true, false, true, 20000, 20000));
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, false, true, false, 1000, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(true, true, true, false, 20000, 20000));
    // an unverified reply is no answer: it waits like silence and drops at 75 s
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(false, true, false, false, 74999, 0));
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, true, false, false, 75000, 0));
}

// Regression: a reply to the sync that does NOT verify (garbled or forged "RM1 0 ...") set replied=1,
// verified=0. The old logic only dropped an entry that had NOT replied, so the chain kept the target's key
// and the single pending slot forever and every later send got "busy".
static void test_unverified_sync_reply_does_not_hang_the_chain(void)
{
    const RmPendAction a = rmPendingDecide(false /*verified*/, true /*replied*/, false, false, 75000, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(RM_PEND_DROP, a, "unverified reply must not hold the chain past 75 s");
    // and far later (the old code would still say WAIT here)
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, true, false, false, 3600000u, 0));
}

static void test_pending_decision_across_millis_wrap(void)
{
    // sync sent 20 s before the wrap, verified 10 s before it: ages are unsigned differences
    const uint32_t syncMs = 0xFFFFFFFFu - 20000u + 1u;   // 2^32 - 20000
    const uint32_t verMs = 0xFFFFFFFFu - 10000u + 1u;    // 2^32 - 10000
    uint32_t now = 499;                                  // 500 ms after the wrap point
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(true, true, false, false, now - syncMs, now - verMs));  // 10499
    now = 500;
    TEST_ASSERT_EQUAL_INT(RM_PEND_SEND, rmPendingDecide(true, true, false, false, now - syncMs, now - verMs));  // 10500
    // silence across the wrap: 74999 vs 75000 ms
    now = 55000u - 1u;
    TEST_ASSERT_EQUAL_INT(RM_PEND_WAIT, rmPendingDecide(false, false, false, false, now - syncMs, 0));
    now = 55000u;
    TEST_ASSERT_EQUAL_INT(RM_PEND_DROP, rmPendingDecide(false, false, false, false, now - syncMs, 0));
}

static void test_chain_error_sentences(void)
{
    const char *toks[] = {"nosync", "lost", "limit", "busy", "send", "store", "ctr", "dst", "cmd"};
    const char *generic = rmErrTokenMessage("no such token at all");
    for (size_t i = 0; i < sizeof(toks) / sizeof(toks[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(strcmp(rmErrTokenMessage(toks[i]), generic) != 0, toks[i]);
        // a static token pointer that outlives a stack buffer
        char buf[16];
        snprintf(buf, sizeof(buf), "%s", toks[i]);
        const char *st = rmTokenStatic(buf, "send");
        TEST_ASSERT_TRUE(st != buf);
        TEST_ASSERT_EQUAL_STRING(toks[i], st);
    }
    TEST_ASSERT_EQUAL_STRING("send", rmTokenStatic("zzz", "send"));
    TEST_ASSERT_EQUAL_STRING("send", rmTokenStatic(nullptr, "send"));
    TEST_ASSERT_NOT_NULL(strstr(rmErrTokenMessage("nosync"), "no answer"));
    TEST_ASSERT_NOT_NULL(strstr(rmErrTokenMessage("nosync"), "not sent"));
}

// ---- status token -----------------------------------------------------------------------------------------

static RmSwitches sw(unsigned bits, bool ledSupported)
{
    RmSwitches s;
    s.gps = bits & 1;
    s.track = bits & 2;
    s.display = bits & 4;
    s.mesh = bits & 8;
    s.gateway = bits & 16;
    s.led = bits & 32;
    s.ledSupported = ledSupported;
    return s;
}

static void test_status_token_letters_and_order(void)
{
    char t[16];
    TEST_ASSERT_EQUAL_UINT(8, rmStatusToken(t, sizeof(t), sw(0x3F, true)));
    TEST_ASSERT_EQUAL_STRING("s=GTDMWL", t);
    TEST_ASSERT_EQUAL_UINT(8, rmStatusToken(t, sizeof(t), sw(0, true)));
    TEST_ASSERT_EQUAL_STRING("s=gtdmwl", t);
    TEST_ASSERT_EQUAL_UINT(8, rmStatusToken(t, sizeof(t), sw(1 | 8, true)));
    TEST_ASSERT_EQUAL_STRING("s=GtdMwl", t);  // gps on, mesh on
    // no LED on this board: the led letter is omitted
    TEST_ASSERT_EQUAL_UINT(7, rmStatusToken(t, sizeof(t), sw(0x3F, false)));
    TEST_ASSERT_EQUAL_STRING("s=GTDMW", t);
    TEST_ASSERT_EQUAL_UINT(7, rmStatusToken(t, sizeof(t), sw(0x20 | 4, false)));  // led bit ignored
    TEST_ASSERT_EQUAL_STRING("s=gtDmw", t);
    // buffer too small: nothing written past n, 0 returned
    char small[8];
    memset(small, 'x', sizeof(small));
    TEST_ASSERT_EQUAL_UINT(0, rmStatusToken(small, 7, sw(0x3F, true)));
    TEST_ASSERT_EQUAL_UINT(0, rmStatusToken(nullptr, 7, sw(0, true)));
}

static void test_status_token_round_trip_all_combinations(void)
{
    for (int led = 0; led < 2; led++)
        for (unsigned bits = 0; bits < 64; bits++)
        {
            if (!led && (bits & 32))
                continue;  // the led bit only exists on boards with an LED
            const int txCur = (int)(bits % 24) - 2;
            char res[96];
            rmFormatStatus(res, sizeof(res), "4.40a", 125, 87, 212, sw(bits, led), txCur, 22);
            RmStatusInfo o;
            TEST_ASSERT_TRUE_MESSAGE(rmStatusParse(res, o), res);
            TEST_ASSERT_TRUE_MESSAGE(o.haveS, res);
            TEST_ASSERT_TRUE_MESSAGE(o.haveP, res);
            TEST_ASSERT_EQUAL_INT_MESSAGE(txCur, o.cur, res);
            TEST_ASSERT_EQUAL_INT_MESSAGE(22, o.max, res);
            TEST_ASSERT_EQUAL_MESSAGE(led != 0, o.ledSupported, res);
            for (unsigned i = 0; i < 5; i++)
                TEST_ASSERT_EQUAL_INT_MESSAGE((bits >> i) & 1, o.sw[i], res);
            if (led)
                TEST_ASSERT_EQUAL_INT_MESSAGE((bits >> 5) & 1, o.sw[5], res);
            else
                TEST_ASSERT_EQUAL_INT_MESSAGE(-1, o.sw[5], res);  // unknown, not "off"
        }
}

static void test_status_format_shape(void)
{
    char res[96];
    const size_t n = rmFormatStatus(res, sizeof(res), "4.40a", 125, 87, 212, sw(1 | 4 | 32, true), 17, 22);
    TEST_ASSERT_EQUAL_STRING("ok v=4.40a up=125 bat=87 heap=212 s=GtDmwL p=17/22 led=1", res);
    TEST_ASSERT_EQUAL_UINT(strlen(res), n);
    // board without LED: no led= field, no L letter
    rmFormatStatus(res, sizeof(res), "4.40a", 125, 87, 212, sw(1 | 4, false), 17, 22);
    TEST_ASSERT_EQUAL_STRING("ok v=4.40a up=125 bat=87 heap=212 s=GtDmw p=17/22", res);
    TEST_ASSERT_NULL(strstr(res, "led="));
    // the old gw= / mesh= fields are gone from the new form
    TEST_ASSERT_NULL(strstr(res, "gw="));
    TEST_ASSERT_NULL(strstr(res, "mesh="));
}

static void test_status_worst_case_length_fits_the_reply(void)
{
    // largest value of every field: millis()/60000 is at most 71582 (2^32 ms), battery 100, heap in kB
    // 4 digits, a signed TX power of 2 digits on both sides, 5 character version, LED present
    char res[160];
    size_t worst = 0;
    const int curs[] = {-99, -20, -9, 0, 9, 22, 99};
    const int maxs[] = {0, 9, 22, 99};
    const uint32_t ups[] = {0, 71582u, 99999u};
    for (unsigned bits = 0; bits < 64; bits += 63)  // all on, all off
        for (size_t a = 0; a < sizeof(curs) / sizeof(curs[0]); a++)
            for (size_t b = 0; b < sizeof(maxs) / sizeof(maxs[0]); b++)
                for (size_t c = 0; c < sizeof(ups) / sizeof(ups[0]); c++)
                {
                    const size_t n = rmFormatStatus(res, sizeof(res), "4.40a", ups[c], 100, 9999, sw(bits, true),
                                                    curs[a], maxs[b]);
                    TEST_ASSERT_EQUAL_UINT(strlen(res), n);
                    if (n > worst)
                        worst = n;
                }
    printf("status worst case: %u of %d\n", (unsigned)worst, RM_MAX_RESULT_REPLY);
    TEST_ASSERT_TRUE(worst <= RM_MAX_RESULT_REPLY);
    // the exact worst string, so a field change shows up here first
    rmFormatStatus(res, sizeof(res), "4.40a", 99999u, 100, 9999, sw(0, true), -99, 99);
    TEST_ASSERT_EQUAL_STRING("ok v=4.40a up=99999 bat=100 heap=9999 s=gtdmwl p=-99/99 led=0", res);
    TEST_ASSERT_EQUAL_UINT(61, strlen(res));

    // out-of-range inputs are clamped, the length stays bounded
    rmFormatStatus(res, sizeof(res), "4.40a", 71582u, 100, 9999, sw(63, true), -1000, 1000);
    TEST_ASSERT_TRUE(strlen(res) <= RM_MAX_RESULT_REPLY);
    TEST_ASSERT_NOT_NULL(strstr(res, "p=-99/99"));
}

static void test_status_format_never_overruns_a_small_buffer(void)
{
    char res[24];
    memset(res, 'x', sizeof(res));
    const size_t n = rmFormatStatus(res, 20, "4.40a", 71582u, 100, 9999, sw(63, true), -99, 99);
    TEST_ASSERT_TRUE(n <= 19);
    TEST_ASSERT_EQUAL_UINT(strlen(res), n);
    TEST_ASSERT_EQUAL_CHAR('x', res[20]);  // nothing past n bytes
    TEST_ASSERT_EQUAL_UINT(0, rmFormatStatus(res, 0, "4.40a", 1, 1, 1, sw(0, true), 0, 0));
    TEST_ASSERT_EQUAL_UINT(0, rmFormatStatus(nullptr, 10, "4.40a", 1, 1, 1, sw(0, true), 0, 0));
}

static void test_status_parse_old_form_and_rejects(void)
{
    RmStatusInfo o;
    // the pre-token reply of older targets: only gw, mesh and led are known
    TEST_ASSERT_TRUE(rmStatusParse("ok v=4.40a up=125 bat=87 heap=212 gw=0 mesh=1", o));
    TEST_ASSERT_FALSE(o.haveS);
    TEST_ASSERT_FALSE(o.haveP);
    TEST_ASSERT_EQUAL_INT(0, o.sw[4]);
    TEST_ASSERT_EQUAL_INT(1, o.sw[3]);
    TEST_ASSERT_EQUAL_INT(-1, o.sw[0]);
    TEST_ASSERT_EQUAL_INT(-1, o.sw[5]);
    TEST_ASSERT_FALSE(o.ledSupported);
    TEST_ASSERT_TRUE(rmStatusParse("ok v=4.40a up=125 bat=87 heap=212 gw=1 mesh=1 led=0", o));
    TEST_ASSERT_TRUE(o.ledSupported);
    TEST_ASSERT_EQUAL_INT(0, o.sw[5]);
    // not a status result / malformed tokens
    TEST_ASSERT_FALSE(rmStatusParse("ok sent", o));
    TEST_ASSERT_FALSE(rmStatusParse("err failed", o));
    TEST_ASSERT_FALSE(rmStatusParse(nullptr, o));
    TEST_ASSERT_FALSE(rmStatusParse("ok v=4.40a s=GTDM", o));        // wrong letter count
    TEST_ASSERT_FALSE(rmStatusParse("ok v=4.40a s=TGDMW", o));       // wrong order
    TEST_ASSERT_FALSE(rmStatusParse("ok v=4.40a s=GTDMWLX", o));
    TEST_ASSERT_TRUE(rmStatusParse("ok v=4.40a s=GTDMW p=x/y", o));  // bad p= ignored, not fatal
    TEST_ASSERT_FALSE(o.haveP);
    TEST_ASSERT_TRUE(rmStatusParse("ok v=4.40a s=GTDMW p=-3/22", o));
    TEST_ASSERT_TRUE(o.haveP);
    TEST_ASSERT_EQUAL_INT(-3, o.cur);
    TEST_ASSERT_EQUAL_INT(22, o.max);
    TEST_ASSERT_FALSE(o.ledSupported);  // 5 letters: no LED on that board
}

static void test_policy_proven_limit_10_and_11th_refused(void)
{
    const uint32_t now = 4000000;
    RmPolEntry e[RM_POLICY_MAX_ENTRIES];
    for (uint8_t i = 0; i < 9; i++)
        e[i] = mk(now - 11000u - 10000u * i);  // 9 unanswered, newest 11 s old
    RmPolDecision d = rmPolicyMaySend(e, 9, now, RM_POLICY_LIMIT_PROVEN);
    TEST_ASSERT_TRUE(d.allowed);
    e[9] = mk(now - 11000u + 10000u * 0);  // 10th, 11 s old
    d = rmPolicyMaySend(e, 10, now, RM_POLICY_LIMIT_PROVEN);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_EQUAL_INT(RM_POL_LIMIT, d.reason);
    TEST_ASSERT_EQUAL_UINT8(10, d.unanswered);
    // the oldest is 91 s old: it leaves the 150 s window in 59 s
    TEST_ASSERT_EQUAL_UINT32(59, d.retryS);
    TEST_ASSERT_TRUE(rmPolicyMaySend(e, 10, now + 59000, RM_POLICY_LIMIT_PROVEN).allowed);
    TEST_ASSERT_FALSE(rmPolicyMaySend(e, 10, now + 58999, RM_POLICY_LIMIT_PROVEN).allowed);
}

static void test_proof_cleared_by_key_change_and_one_shot_rules(void)
{
    RmProof b[RM_PROOF_N];
    memset(b, 0, sizeof(b));
    const uint8_t a[4] = {1, 2, 3, 4}, c[4] = {5, 6, 7, 8};
    RmProof *p = rmProofSetKey(b, "DK5EN-90", a);
    TEST_ASSERT_FALSE(p->oneShot);
    rmProofVerified(b, "DK5EN-90", c);  // a reply under another key proves nothing
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));
    rmProofVerified(b, "DK5EN-90", a);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));  // proven but cap unknown: still 2
    rmProofSetCap(b, "DK5EN-90", a, 2);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, rmProofLimit(p));
    rmProofSetKey(b, "DK5EN-90", a);  // same key: proof stays, nothing armed
    TEST_ASSERT_TRUE(p->proven);
    TEST_ASSERT_FALSE(p->oneShot);
    rmProofSetKey(b, "DK5EN-90", c);  // re-key: proof gone, one-shot armed
    TEST_ASSERT_FALSE(p->proven);
    TEST_ASSERT_TRUE(p->oneShot);
    rmProofForget(b, "DK5EN-90");
    TEST_ASSERT_NULL(rmProofFind(b, "DK5EN-90"));
    TEST_ASSERT_FALSE(rmProofSetKey(b, "DK5EN-90", c)->oneShot);  // forgotten: first key arms nothing

    // spacing is not overridden by the one-shot; an allowed send does not use it
    const uint32_t now = 4000000;
    RmPolEntry e[2] = {mk(now - 5000), mk(now - 15000)};
    RmPolDecision d = rmPolicyMaySend(e, 2, now, 2, true, true);
    TEST_ASSERT_FALSE(d.allowed);
    TEST_ASSERT_TRUE(d.canForce);
    TEST_ASSERT_FALSE(d.usedForce);
    RmPolEntry f[1] = {mk(now - 15000)};
    d = rmPolicyMaySend(f, 1, now, 2, true, true);
    TEST_ASSERT_TRUE(d.allowed);
    TEST_ASSERT_FALSE(d.usedForce);
}

static void test_book_never_evicts_counted_entries_and_goes_busy(void)
{
    const uint32_t now = 4000000;
    RmPolEntry e[12];
    for (uint8_t i = 0; i < 12; i++)
        e[i] = mk(now - 1000u - 5000u * i);
    TEST_ASSERT_EQUAL_INT(-1, rmBookVictim(e, 12, 12, now));  // all counted: busy
    TEST_ASSERT_EQUAL_INT(-2, rmBookVictim(e, 11, 12, now));  // a free slot
    e[7] = mk(now - 30000, true, true, false);                // answered: no longer counts
    e[10] = mk(now - 200000, false, false, false, true);      // expired
    TEST_ASSERT_EQUAL_INT(10, rmBookVictim(e, 12, 12, now));  // the OLDEST non-counting one
    TEST_ASSERT_EQUAL_UINT32(12, RM_POLICY_MAX_ENTRIES);
}

void test_book_room_counts_free_and_stale_slots(void)
{
    RmPolEntry e[12];
    memset(e, 0, sizeof(e));
    for (int i = 0; i < 12; i++)
        e[i].sentMs = 100000u;  // all unanswered, young
    TEST_ASSERT_EQUAL_UINT8(0, rmBookRoom(e, 12, 12, 101000u));
    TEST_ASSERT_EQUAL_UINT8(2, rmBookRoom(e, 10, 12, 101000u));  // two free slots
    e[11].verified = true;  // answered: no longer counts
    TEST_ASSERT_EQUAL_UINT8(1, rmBookRoom(e, 12, 12, 101000u));
    e[10].expired = true;
    TEST_ASSERT_EQUAL_UINT8(2, rmBookRoom(e, 12, 12, 101000u));
    TEST_ASSERT_EQUAL_UINT8(12, rmBookRoom(e, 12, 12, 100000u + RM_POLICY_WINDOW_MS));  // all aged out
}

// Capability gate: the proven budget 10 needs proven AND cap >= 2 (the target's sync reply says rm=2, i.e. it does not
// count authenticated replays); every other target keeps the unproven budget 2, which is safe against the old receiver.
static void test_proof_cap_gates_the_proven_limit(void)
{
    RmProof b[RM_PROOF_N];
    memset(b, 0, sizeof(b));
    const uint8_t a[4] = {1, 2, 3, 4}, c[4] = {5, 6, 7, 8};
    RmProof *p = rmProofSetKey(b, "DK5EN-90", a);
    TEST_ASSERT_EQUAL_UINT8(0, p->cap);
    rmProofVerified(b, "DK5EN-90", a);
    TEST_ASSERT_TRUE(p->proven);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));  // proven + cap 0 (old or unknown): 2
    rmProofSetCap(b, "DK5EN-90", a, 1);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));  // proven + cap 1: 2
    rmProofSetCap(b, "DK5EN-90", c, 2);                                  // another fingerprint: ignored
    TEST_ASSERT_EQUAL_UINT8(1, p->cap);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));
    rmProofSetCap(b, "DK5EN-91", a, 2);                                  // unknown target: nothing happens
    TEST_ASSERT_NULL(rmProofFind(b, "DK5EN-91"));
    rmProofSetCap(b, "DK5EN-90", a, 2);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, rmProofLimit(p));    // proven + cap 2: 10
    rmProofSetKey(b, "DK5EN-90", a);                                     // same key: cap stays
    TEST_ASSERT_EQUAL_UINT8(2, p->cap);

    // unproven + cap 2 stays at 2
    rmProofForget(b, "DK5EN-90");
    p = rmProofSetKey(b, "DK5EN-90", a);
    rmProofSetCap(b, "DK5EN-90", a, 2);
    TEST_ASSERT_FALSE(p->proven);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(nullptr));

    // a key change resets cap together with proven
    rmProofVerified(b, "DK5EN-90", a);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_PROVEN, rmProofLimit(p));
    rmProofSetKey(b, "DK5EN-90", c);
    TEST_ASSERT_FALSE(p->proven);
    TEST_ASSERT_EQUAL_UINT8(0, p->cap);
    rmProofVerified(b, "DK5EN-90", c);
    TEST_ASSERT_EQUAL_UINT8(RM_POLICY_LIMIT_UNPROVEN, rmProofLimit(p));  // proven again, but the new key's cap is unknown

    // forget clears it all
    rmProofSetCap(b, "DK5EN-90", c, 2);
    rmProofForget(b, "DK5EN-90");
    p = rmProofSetKey(b, "DK5EN-90", c);
    TEST_ASSERT_EQUAL_UINT8(0, p->cap);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_password_table);
    RUN_TEST(test_leading_space_password_is_rejected);
    RUN_TEST(test_password_never_truncates);
    RUN_TEST(test_every_password_problem_has_a_sentence);
    RUN_TEST(test_call_table);
    RUN_TEST(test_state_classification_and_boundaries);
    RUN_TEST(test_state_across_millis_wrap);
    RUN_TEST(test_state_names_and_messages);
    RUN_TEST(test_every_error_token_has_a_sentence);
    RUN_TEST(test_policy_free_target_may_send);
    RUN_TEST(test_policy_cooldown_boundary);
    RUN_TEST(test_policy_two_unanswered_inside_90s_lock_the_third);
    RUN_TEST(test_policy_three_and_more_unanswered_wait_for_enough_to_leave);
    RUN_TEST(test_policy_answered_and_aged_out_do_not_count);
    RUN_TEST(test_policy_cooldown_and_limit_combine_to_the_longer_wait);
    RUN_TEST(test_policy_across_millis_wrap);
    RUN_TEST(test_policy_entry_cap_is_safe);
    RUN_TEST(test_policy_proven_limit_10_and_11th_refused);
    RUN_TEST(test_proof_cap_gates_the_proven_limit);
    RUN_TEST(test_proof_cleared_by_key_change_and_one_shot_rules);
    RUN_TEST(test_book_never_evicts_counted_entries_and_goes_busy);
    RUN_TEST(test_auto_sync_is_conditional);
    RUN_TEST(test_pending_decision_table);
    RUN_TEST(test_unverified_sync_reply_does_not_hang_the_chain);
    RUN_TEST(test_pending_decision_across_millis_wrap);
    RUN_TEST(test_chain_error_sentences);
    RUN_TEST(test_status_token_letters_and_order);
    RUN_TEST(test_status_token_round_trip_all_combinations);
    RUN_TEST(test_status_format_shape);
    RUN_TEST(test_status_worst_case_length_fits_the_reply);
    RUN_TEST(test_status_format_never_overruns_a_small_buffer);
    RUN_TEST(test_status_parse_old_form_and_rejects);
    RUN_TEST(test_book_room_counts_free_and_stale_slots);
    return UNITY_END();
}
