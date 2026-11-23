/**
 * TEMPORARY MEASUREMENT SCAFFOLDING (OTA-LOWHEAP-01 wave 1b) -- INSTRUMENT builds, ESP32 only.
 *
 * Triggers single functions one at a time and prints the internal-RAM use of each:
 *   [INSTR-PROBE];name;<id>;phase;start;ifree;<B>;iblk;<B>
 *   [INSTR-PROBE];name;<id>;phase;end;ifree;<B>;iblk;<B>;low;<B>;peak_used;<B>;delta;<B>;dt_ms;<ms>;rc;<ok|timeout|err>
 *   [INSTR-PROBE];list;<id>;<description>
 * Output goes through printfdeb() (USB serial and the 2323 net console). dns and tls run in a worker task
 * (8 kB stack, counted in peak_used, announced by a [INSTR-PROBE];note line). Every probe holds off while an AU
 * job is busy ("busy;au"). wifi_scan needs STA already enabled; page_* refuse when the command came from the
 * web GUI. low = heapwatch low mark if known, else the floor of the tick-sampled internal free.
 * peak_used = start ifree - low; delta = start ifree - end ifree (positive = not given back).
 *
 * No probe transmits on LoRa, sends a mesh message, talks to the MeshCom server/gateway or
 * changes settings. On other targets or without INSTRUMENT_ENABLED both functions are no-ops
 * (the first prints "unavailable").
 */
#pragma once

void instrument_heapprobe(const char *arg); // "list" | "<id>" | "all" (10 s settle gap); blanks around arg ignored
void instrument_probe_tick(void);           // call from the loop tick; drives async probes and "all"
