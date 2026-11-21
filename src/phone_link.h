// Phone-link arbiter: one phone session, owner BLE or NET, newest authenticated
// session wins (docs/applink-api-contract.md section 6). Header-only, no deps.
#ifndef PHONE_LINK_H
#define PHONE_LINK_H

#include <stdint.h>

enum PhoneLinkOwner : uint8_t
{
    PL_NONE = 0,
    PL_BLE = 1,
    PL_NET = 2
};

struct PhoneLinkState
{
    PhoneLinkOwner owner;
    uint32_t takeovers;
};

static inline void phoneLinkInit(PhoneLinkState *s)
{
    s->owner = PL_NONE;
    s->takeovers = 0;
}

// Returns the owner that must now be closed (the other transport) or PL_NONE.
static inline PhoneLinkOwner phoneLinkClaim(PhoneLinkState *s, PhoneLinkOwner who)
{
    if (who == PL_NONE)
        return PL_NONE;
    PhoneLinkOwner prev = s->owner;
    s->owner = who;
    if (prev == PL_NONE || prev == who)
        return PL_NONE;
    s->takeovers++;
    return prev;
}

// True if the phone session ended (who was the owner).
static inline bool phoneLinkRelease(PhoneLinkState *s, PhoneLinkOwner who)
{
    if (who == PL_NONE || s->owner != who)
        return false;
    s->owner = PL_NONE;
    return true;
}

static inline bool phoneLinkOwnedBy(const PhoneLinkState *s, PhoneLinkOwner who)
{
    return s->owner == who;
}

static inline bool phoneLinkUp(const PhoneLinkState *s)
{
    return s->owner != PL_NONE;
}

// A BLE "link down" edge (nRF52 disconnect callback, ESP32 edge in the loop) ends the phone session only if
// BLE owns it AND no BLE link is up at the moment the loop handles the edge. A new central can connect and
// finish its hello between the drop of the previous one and that handling (the nRF52 loop drains its BLE
// queue before nrf52BleTick()); the edge belongs to the old central then, the new session must stay.
static inline bool phoneLinkBleDownEndsSession(const PhoneLinkState *s, bool bleLinkUpNow)
{
    return s->owner == PL_BLE && !bleLinkUpNow;
}


#endif // PHONE_LINK_H
