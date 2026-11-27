// Rig stand-in for src/watchdog_feed.h (found first through -I test/test_web_diff/stubs): the same entry point,
// but it counts the calls in the rig state, so a test sees that a loop feeds the task watchdog.
#pragma once
#ifndef _WATCHDOG_FEED_H_
#define _WATCHDOG_FEED_H_

void webRigWdtFeed(); // rig/web_rig.cpp
static inline void meshcom_wdt_feed(void) { webRigWdtFeed(); }

#endif
