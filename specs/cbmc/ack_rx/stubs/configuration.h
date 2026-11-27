// Minimal stand-in for the board configuration.h: only what ack_rx_core.h and its headers use.
// Values are the firmware's (configuration_global.h: MAX_RING 20, MAX_DEDUP_RING 100 on the classic ESP32/nRF52 large
// profile, MAX_HOP_LIMIT 7, MSG_TYPE_ACK 0x41). -DMAX_DEDUP_RING / -DMAX_RING override for a smaller bound.
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifndef MAX_RING
#define MAX_RING 20
#endif
#ifndef MAX_DEDUP_RING
#define MAX_DEDUP_RING 100
#endif
#define MAX_HOP_LIMIT 7
#define MSG_TYPE_ACK 0x41
