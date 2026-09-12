/**
 * @file settings_store_nrf52.h
 * @brief nRF52 file backend for the keyed settings store (D1-04 target
 *        architecture, docs/BACKLOG.md OPT-07 sizing).
 *
 * THIS WAVE BUILDS THE MECHANISM ONLY -- it is wired into nothing. No call
 * from init_flash(), no call from any main loop, no call from any command
 * handler. Cutover (replacing the sizeof()-struct raw read/write in
 * nrf52_flash.cpp with calls to settingsStoreSave()/settingsStoreLoad()) is
 * later, bench-hardware-gated wave work: a mistake there costs every node in
 * the fleet its settings, so it does not happen in the same wave that first
 * writes this code.
 *
 * settings_store.h (the codec) and settings_schema.h (the field table, a
 * sibling deliverable of this same campaign) are the two things this file
 * depends on. See settings_store_nrf52.cpp for exactly how the write path
 * gets its atomicity and why the config_json.cpp layout-version gate must
 * NOT appear here.
 */
#pragma once

#ifdef NRF52_SERIES

#include "settings_store.h"

// Outcome of settingsStoreLoad(). A plain bool return can't distinguish "no
// settings file exists yet" (expected on first boot / after a format) from
// "a file exists but nothing in it decoded" -- both would otherwise look
// like the same false. DecodeStats already carries everything interesting
// about a decode that did run (settings_store.h), so this just adds the two
// bits DecodeStats itself has no way to express: whether a file was even
// there, and whether the read that would produce those stats completed.
struct SettingsLoadResult
{
	// false: no settings file existed to open (e.g. first boot after
	// InternalFS.format(), or the live path was never created). stats is
	// default-constructed (all zero) in this case -- decode() never ran.
	bool file_found = false;

	// true: the file existed, was opened and read without a filesystem
	// error, and settings_store::decode() ran over its content. False means
	// the file existed but could not be read (open/read failure, or a
	// malloc failure for the decode buffer) -- stats is meaningless then.
	bool read_ok = false;

	// Only meaningful when read_ok is true. See settings_store.h's
	// DecodeStats for what each count means; in particular a non-zero
	// malformed_lines/unknown_keys count is NOT itself a failure (that is
	// the whole point of the keyed store), only something a caller may want
	// to log.
	settings_store::DecodeStats stats;
};

// Encodes meshcom_settings through settings_schema::fields()/fieldCount()
// and writes the result to the settings file, replacing whatever was there
// before via a temp-file-then-rename sequence (see the .cpp for exactly
// what atomicity that does and does not provide). Returns false, leaving
// the previously-persisted file completely untouched, on any failure:
// allocation failure, settings_store::encode() overflowing the buffer, or a
// filesystem error opening/writing the temp file or renaming it into place.
bool settingsStoreSave();

// Reads the settings file (if any) and decodes it into meshcom_settings via
// settings_schema::fields()/fieldCount(). See SettingsLoadResult above for
// how to read what happened; meshcom_settings fields whose key was missing
// or malformed in the file are left at whatever value the caller already
// had in meshcom_settings before calling this (settings_store.h's decode()
// contract) -- callers that want struct-default fallback behaviour must
// ensure meshcom_settings already holds those defaults before calling this,
// exactly as today's sanitize/default path does.
SettingsLoadResult settingsStoreLoad();

// Removes the keyed store's live file and its temp file (if either exists),
// for flash_reset()'s targeted reset (nrf52_flash.cpp) -- an alternative to
// InternalFS.format(), which would also erase every OTHER file on the
// filesystem. "The file did not exist" is not a failure (expected on a
// first-ever reset); this returns false only when a file that DID exist
// could not be removed, which is flash_reset()'s signal to fall back to
// format().
bool settingsStoreRemove();

// Prints the raw contents of the keyed store to Serial, for bench diagnosis.
// Returns false if the file does not exist or cannot be read.
bool settingsStoreDump(void);

#endif // NRF52_SERIES
