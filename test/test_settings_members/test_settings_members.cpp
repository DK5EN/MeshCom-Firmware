// test_settings_members.cpp -- the member-level fail-closed gate for
// struct s_meshcom_settings (D1-04 "target architecture", W3 step 3;
// docs/BACKLOG.md).
//
// src/meshcom_settings.h defines the struct ONCE, from three X-macro lists
// (MESHCOM_SETTINGS_MEMBERS_COMMON/_ESP32/_TDECK). This suite enumerates
// EVERY member those lists produce -- via offsetof()/sizeof(), never a
// hand-copied name list, so an add/remove/rename in the struct is picked up
// automatically the next time this suite runs -- and proves each one is
// EXACTLY one of:
//
//   (a) covered by settings_schema::fields(): either one descriptor whose
//       byte range equals the member's own range (a scalar), or, for an
//       array member, a SET of descriptors whose ranges TILE the member's
//       byte range with no gap and no overlap (settings_schema addresses
//       each array index as its own row, e.g. "node_gcb0".."node_gcb5" for
//       node_gcb[0]..node_gcb[5]);
//   (b) named in MESHCOM_SETTINGS_RUNTIME (src/meshcom_settings_runtime.h),
//       the explicit "deliberately not persisted" list.
//
// A member in NEITHER set silently never persists -- the exact DR-12/DR-13
// defect class this whole D1-04 campaign exists to close, just moved one
// level down from "two structs disagree" to "one struct, one member,
// nobody decided". A member in BOTH sets is equally wrong: it cannot be
// simultaneously schema-persisted and declared deliberately runtime-only.
// This suite fails closed -- collects every offender into ONE failure
// message -- on either shape.
//
// Run twice, once per platform's WIDEST view (see platformio.ini):
//   pio test -e native_settings_members_esp32   (ESP32 + BOARD_T_DECK, the
//                                                 widest struct)
//   pio test -e native_settings_members_nrf52
//
// Both envs compile ONLY src/settings_store.cpp + src/settings_schema.cpp
// alongside this file (build_src_filter) -- settings_schema.cpp's own
// includes (config_json.h, maxhop.h, settings_store.h, meshcom_settings.h)
// are all self-contained (stdint.h/stddef.h only) except for one
// __has_include(<configuration.h>) probe that resolves to "not found" on
// this include path, in which case settings_schema.cpp's own
// TX_POWER_MIN/MAX fallbacks apply -- both envs additionally supply the
// real per-board values via -D so the fallback is never exercised here
// either. Nothing under stubs/ is needed today; the directory exists so a
// future dependency has somewhere to land without an env edit.
#include <unity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <meshcom_settings.h>
#include <meshcom_settings_runtime.h>
#include <settings_schema.h>
#include <settings_store.h>

// ---------------------------------------------------------------------------
// Member enumeration -- offsetof()/sizeof() per row of the SAME X-macro
// lists meshcom_settings.h expands to build the struct itself
// (MESHCOM_SETTINGS_MEMBERS(M, A), meshcom_settings.h's own union of
// _COMMON/_ESP32/_TDECK). Both M(type, name, default) and A(type, name,
// bounds, init...) rows are matched by the SAME macro here: name is always
// the second argument (offsetof/sizeof only ever need the name and the
// struct itself; a variadic tail is what lets a braced array initialiser
// with its own top-level commas survive as one argument, exactly per
// meshcom_settings.h's own "Row shapes" comment -- this file just never
// looks at that tail).
// ---------------------------------------------------------------------------
struct MemberInfo
{
    const char *name;
    size_t offset;
    size_t size;
};

#define MC_MEMBER_ROW(type, name, ...) \
    {#name, offsetof(s_meshcom_settings, name), sizeof(((s_meshcom_settings *)0)->name)},

static const MemberInfo kMembers[] = {
    MESHCOM_SETTINGS_MEMBERS(MC_MEMBER_ROW, MC_MEMBER_ROW)};
#undef MC_MEMBER_ROW

static const size_t kMemberCount = sizeof(kMembers) / sizeof(kMembers[0]);

// ---------------------------------------------------------------------------
// Runtime-name membership -- src/meshcom_settings_runtime.h's
// MESHCOM_SETTINGS_RUNTIME(R) list.
//
// Resolved at COMPILE TIME by construction, not merely parsed as strings at
// run time: the SAME MESHCOM_SETTINGS_RUNTIME(R) list is expanded a second
// time below into a `sizeof(((s_meshcom_settings*)0)->name)` reference for
// every name. A stale R(name) -- a member renamed or removed from the
// struct but left behind in meshcom_settings_runtime.h -- makes THAT
// expansion fail to compile; it can never silently linger as a "runtime"
// name for a member that no longer exists. See
// mc_runtime_names_are_real_members() below, which exists ONLY to hold that
// expansion (it is never called from anywhere, including this file --
// compiling it is the entire check).
// ---------------------------------------------------------------------------
#define MC_RUNTIME_VERIFY(name) (void)sizeof(((s_meshcom_settings *)0)->name);
[[maybe_unused]] static void mc_runtime_names_are_real_members()
{
    MESHCOM_SETTINGS_RUNTIME(MC_RUNTIME_VERIFY)
}
#undef MC_RUNTIME_VERIFY

#define MC_RUNTIME_STR(name) #name,
static const char *const kRuntimeNames[] = {
    MESHCOM_SETTINGS_RUNTIME(MC_RUNTIME_STR)
        nullptr // sentinel -- keeps the array well-formed even if the list is ever empty
};
#undef MC_RUNTIME_STR

static bool is_runtime_name(const char *name)
{
    for (size_t i = 0; kRuntimeNames[i] != nullptr; i++)
        if (std::string(kRuntimeNames[i]) == name)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Schema coverage -- does settings_schema::fields() TILE this member's byte
// range exactly? One row for a scalar; several contiguous, non-overlapping
// rows for an array. Any descriptor that only partially overlaps the member
// (straddles its bounds) is deliberately NOT treated as coverage here --
// test_every_descriptor_lies_inside_one_member is the dedicated check for
// that shape, so it is reported once, not duplicated into this test's
// findings too.
// ---------------------------------------------------------------------------
static std::vector<size_t> intersecting_descriptor_indices(
    size_t memberOffset, size_t memberSize,
    const settings_store::FieldDescriptor *fields, size_t fieldCount)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < fieldCount; i++)
    {
        const auto &d = fields[i];
        if (d.offset < memberOffset + memberSize && d.offset + d.size > memberOffset)
            out.push_back(i);
    }
    std::sort(out.begin(), out.end(),
               [&](size_t a, size_t b) { return fields[a].offset < fields[b].offset; });
    return out;
}

static bool schema_fully_tiles_member(size_t memberOffset, size_t memberSize,
                                       const settings_store::FieldDescriptor *fields,
                                       size_t fieldCount)
{
    std::vector<size_t> idx =
        intersecting_descriptor_indices(memberOffset, memberSize, fields, fieldCount);
    if (idx.empty())
        return false;
    size_t cursor = memberOffset;
    for (size_t i : idx)
    {
        const auto &d = fields[i];
        if (d.offset < memberOffset || d.offset + d.size > memberOffset + memberSize)
            return false; // straddles the member's own bounds -- not a clean tiling
        if (d.offset != cursor)
            return false; // gap, or overlap with the previous descriptor
        cursor = d.offset + d.size;
    }
    return cursor == memberOffset + memberSize; // no trailing gap either
}

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// test_every_member_is_schema_or_runtime
// ---------------------------------------------------------------------------
static void test_every_member_is_schema_or_runtime()
{
    const settings_store::FieldDescriptor *fields = settings_schema::fields();
    const size_t fieldCount = settings_schema::fieldCount();

    std::string offenders;
    size_t offenderCount = 0;
    for (size_t i = 0; i < kMemberCount; i++)
    {
        const MemberInfo &m = kMembers[i];
        const bool schema = schema_fully_tiles_member(m.offset, m.size, fields, fieldCount);
        const bool runtime = is_runtime_name(m.name);
        if (schema == runtime) // both true, or both false -- exactly one is required
        {
            char line[256];
            snprintf(line, sizeof(line), "  %s (offset=%zu size=%zu): %s\n", m.name, m.offset,
                      m.size,
                      schema ? "in BOTH settings_schema::fields() and MESHCOM_SETTINGS_RUNTIME"
                             : "in NEITHER settings_schema::fields() NOR MESHCOM_SETTINGS_RUNTIME");
            offenders += line;
            offenderCount++;
        }
    }

    char summary[160];
    snprintf(summary, sizeof(summary),
              "%zu of %zu member(s) failed the schema-or-runtime gate (exactly one required)%s",
              offenderCount, kMemberCount, offenderCount ? ":\n" : "");
    std::string report = std::string(summary) + offenders;
    TEST_ASSERT_TRUE_MESSAGE(offenderCount == 0, report.c_str());
}

// ---------------------------------------------------------------------------
// test_every_descriptor_lies_inside_one_member
// ---------------------------------------------------------------------------
static void test_every_descriptor_lies_inside_one_member()
{
    const settings_store::FieldDescriptor *fields = settings_schema::fields();
    const size_t fieldCount = settings_schema::fieldCount();
    const size_t structSize = sizeof(s_meshcom_settings);

    std::string offenders;
    size_t offenderCount = 0;
    for (size_t i = 0; i < fieldCount; i++)
    {
        const auto &d = fields[i];
        if (d.offset + d.size > structSize)
        {
            char line[256];
            snprintf(line, sizeof(line),
                      "  \"%s\" (offset=%zu size=%zu) extends past sizeof(s_meshcom_settings)=%zu\n",
                      d.key, d.offset, d.size, structSize);
            offenders += line;
            offenderCount++;
            continue;
        }

        size_t containingCount = 0;
        for (size_t m = 0; m < kMemberCount; m++)
        {
            const MemberInfo &mem = kMembers[m];
            const bool fullyInside =
                d.offset >= mem.offset && d.offset + d.size <= mem.offset + mem.size;
            const bool intersects =
                d.offset < mem.offset + mem.size && d.offset + d.size > mem.offset;
            if (intersects && !fullyInside)
            {
                char line[256];
                snprintf(line, sizeof(line),
                          "  \"%s\" (offset=%zu size=%zu) straddles member %s (offset=%zu size=%zu)\n",
                          d.key, d.offset, d.size, mem.name, mem.offset, mem.size);
                offenders += line;
                offenderCount++;
            }
            else if (fullyInside)
            {
                containingCount++;
            }
        }
        if (containingCount == 0)
        {
            char line[256];
            snprintf(line, sizeof(line),
                      "  \"%s\" (offset=%zu size=%zu) does not lie inside any enumerated member\n",
                      d.key, d.offset, d.size);
            offenders += line;
            offenderCount++;
        }
    }

    char summary[160];
    snprintf(summary, sizeof(summary),
              "%zu descriptor(s) out of %zu straddle a member boundary or lie outside the struct%s",
              offenderCount, fieldCount, offenderCount ? ":\n" : "");
    std::string report = std::string(summary) + offenders;
    TEST_ASSERT_TRUE_MESSAGE(offenderCount == 0, report.c_str());
}

// ---------------------------------------------------------------------------
// test_member_sizes_and_padding_sum_to_sizeof
// ---------------------------------------------------------------------------
static void test_member_sizes_and_padding_sum_to_sizeof()
{
    std::vector<MemberInfo> sorted(kMembers, kMembers + kMemberCount);
    std::sort(sorted.begin(), sorted.end(),
               [](const MemberInfo &a, const MemberInfo &b) { return a.offset < b.offset; });

    std::string offenders;
    size_t offenderCount = 0;
    size_t cursor = 0; // the leading gap (0 .. first member's offset) counts too
    size_t total = 0;
    for (const MemberInfo &m : sorted)
    {
        if (m.offset < cursor)
        {
            char line[256];
            snprintf(line, sizeof(line),
                      "  member %s (offset=%zu) overlaps the previous member (which ends at "
                      "offset=%zu)\n",
                      m.name, m.offset, cursor);
            offenders += line;
            offenderCount++;
            // Still advance total by the member's own size so a single
            // overlap does not cascade into a wall of misleading "gap"
            // arithmetic for every member after it.
            total += m.size;
            cursor = std::max(cursor, m.offset + m.size);
            continue;
        }
        const size_t gap = m.offset - cursor;
        total += gap + m.size;
        cursor = m.offset + m.size;
    }
    const size_t structSize = sizeof(s_meshcom_settings);
    const size_t trailingGap = (structSize > cursor) ? (structSize - cursor) : 0;
    total += trailingGap;

    char summary[256];
    snprintf(summary, sizeof(summary),
              "sum of member sizes + inter-member gaps + trailing padding = %zu, "
              "sizeof(s_meshcom_settings) = %zu%s",
              total, structSize, offenderCount ? " -- also found overlap(s):\n" : "");
    std::string report = std::string(summary) + offenders;
    TEST_ASSERT_TRUE_MESSAGE(offenderCount == 0 && total == structSize, report.c_str());
}

// ---------------------------------------------------------------------------
// test_no_duplicate_descriptor_keys
// ---------------------------------------------------------------------------
static void test_no_duplicate_descriptor_keys()
{
    const settings_store::FieldDescriptor *fields = settings_schema::fields();
    const size_t fieldCount = settings_schema::fieldCount();

    std::string offenders;
    size_t offenderCount = 0;
    for (size_t i = 0; i < fieldCount; i++)
        for (size_t j = i + 1; j < fieldCount; j++)
            if (std::string(fields[i].key) == fields[j].key)
            {
                char line[256];
                snprintf(line, sizeof(line), "  \"%s\" appears at descriptor index %zu and %zu\n",
                          fields[i].key, i, j);
                offenders += line;
                offenderCount++;
            }

    char summary[160];
    snprintf(summary, sizeof(summary), "%zu duplicate key pair(s) among %zu descriptor(s)%s",
              offenderCount, fieldCount, offenderCount ? ":\n" : "");
    std::string report = std::string(summary) + offenders;
    TEST_ASSERT_TRUE_MESSAGE(offenderCount == 0, report.c_str());
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_every_member_is_schema_or_runtime);
    RUN_TEST(test_every_descriptor_lies_inside_one_member);
    RUN_TEST(test_member_sizes_and_padding_sum_to_sizeof);
    RUN_TEST(test_no_duplicate_descriptor_keys);

    return UNITY_END();
}
