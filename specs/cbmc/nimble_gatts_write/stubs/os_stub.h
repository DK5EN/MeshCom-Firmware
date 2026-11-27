// Stand-in for os/os.h of the NimBLE porting layer (copied to gen/os/os.h next to the real os_mbuf.h / queue.h).
// Only what os_mbuf.h needs; the real os.h pulls syscfg and the FreeRTOS NPL.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "queue.h"
struct ble_npl_event { void *arg; };
struct ble_npl_eventq { int dummy; };
typedef void ble_npl_event_fn(struct ble_npl_event *ev);
typedef uint32_t ble_npl_time_t;
