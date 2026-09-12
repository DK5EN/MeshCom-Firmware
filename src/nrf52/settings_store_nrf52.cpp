/**
 * @file settings_store_nrf52.cpp
 * @brief nRF52 file backend for the keyed settings store. See
 *        settings_store_nrf52.h for scope (mechanism only, wired into
 *        nothing yet) and settings_store.h for the codec contract this
 *        wraps.
 */
#ifdef NRF52_SERIES

#include "settings_store_nrf52.h"

#include <cstdlib>
#include <cstring>   // memcmp, for the skip-if-unchanged compare below

#include <debugconf.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;

#include "WisBlock-API.h" // extern s_meshcom_settings meshcom_settings;
#include "settings_schema.h" // settings_schema::fields() / fieldCount()

namespace
{

// Worst-case encoded size for the full persist set, string-keyed, measured
// in docs/opt07-nrf52-settings-store-sizing-20260912.md: 2 937 B against
// 149 304 B free flash on wiscore_rak4631 (the tightest nRF52 env). This
// constant is the BUFFER CAP, not the measured worst case itself, so it
// needs headroom above 2 937 B: 4096 B gives ~1.4x margin over the measured
// figure while still being a small fraction of the 149 304 B free-flash
// budget the same document reports -- a round power of two costs nothing
// extra there, so there was no reason to cut it closer.
constexpr size_t kSettingsBufferCap = 4096;

const char kSettingsPath[] = "/MeshCom-Settings-Store";
const char kSettingsTmpPath[] = "/MeshCom-Settings-Store.tmp";

// Adafruit_LittleFS::open()'s own doc comment: "Note that currently only
// one file can be open at a time." One File object, opened and closed
// around each single use, mirrors the `lora_file` idiom nrf52_flash.cpp
// already uses for the very same reason.
File settings_store_file(InternalFS);

} // namespace

bool settingsStoreSave()
{
	char *buf = (char *)malloc(kSettingsBufferCap);
	if (buf == nullptr)
	{
		DEBUG_MSG("SETST", "save: malloc(%u) failed", (unsigned)kSettingsBufferCap);
		return false;
	}

	long written = settings_store::encode(settings_schema::fields(), settings_schema::fieldCount(),
										   &meshcom_settings, buf, kSettingsBufferCap);
	if (written < 0)
	{
		// encode() is all-or-nothing (settings_store.h): a negative return means the full record
		// did not fit kSettingsBufferCap and NOTHING was written to buf. Growing kSettingsBufferCap
		// is the fix if this ever fires for real; it means the schema grew past the OPT-07 sizing
		// headroom, not that this call site is doing anything wrong.
		DEBUG_MSG("SETST", "save: encode() overflowed the %u B buffer", (unsigned)kSettingsBufferCap);
		free(buf);
		return false;
	}

	// ---------------------------------------------------------------------
	// Skip the write entirely when the encoded content is byte-identical to
	// what is already on the filesystem.
	//
	// This is NOT an optimisation bolted on: the raw-blit save_settings()
	// this store replaces read the stored struct back and did a memcmp
	// before writing ("Flash content changed, writing new data"), and that
	// guard has to survive the cutover. Its existence is the evidence for
	// why: save_settings() has 150+ call sites across command_functions.cpp,
	// phone_commands.cpp, loop_functions.cpp and event_functions.cpp, and
	// nothing makes them call only on an actual change. Dropping the guard
	// would turn every one of those calls into a temp-file write plus a
	// rename on the nRF52's internal flash -- a wear increase that would
	// show up in the field, months later, as a filesystem that stopped
	// taking writes.
	//
	// Compared in chunks against a small stack buffer rather than by slurping
	// the file into a second heap allocation: the encoded record is already
	// kSettingsBufferCap bytes of heap, and this runs on the main-loop task
	// whose stack is 4 KB.
	// ---------------------------------------------------------------------
	if (settings_store_file.open(kSettingsPath, FILE_O_READ))
	{
		bool identical = (settings_store_file.size() == (uint32_t)written);
		if (identical)
		{
			uint8_t chunk[64];
			long off = 0;
			while (off < written)
			{
				int want = (int)((written - off) < (long)sizeof(chunk) ? (written - off) : (long)sizeof(chunk));
				int got = settings_store_file.read(chunk, want);
				if (got != want || memcmp(chunk, buf + off, (size_t)got) != 0)
				{
					identical = false;
					break;
				}
				off += got;
			}
		}
		settings_store_file.close();

		if (identical)
		{
			free(buf);
			return true;
		}
	}

	// ---------------------------------------------------------------------
	// Atomicity: write the FULL new content to a temp path, verify every
	// byte of it landed, and only then replace the live path -- the live
	// path itself is never opened for writing. Adafruit_LittleFS exposes a
	// real rename() (Adafruit_LittleFS.h), and littlefs's own lfs_rename()
	// (littlefs/lfs.h) both replaces an existing destination of matching
	// type and is a single atomic metadata update -- littlefs is a
	// power-loss-safe filesystem by design, so that swap step has no
	// partial-write window: after any reset, kSettingsPath is either the
	// old content or the new content, never a mix.
	//
	// The temp-file WRITE ahead of the rename is NOT covered by that
	// guarantee -- a power loss while writing kSettingsTmpPath can leave a
	// partial/corrupt temp file. That is the window this sequence leaves
	// open, and it is the safe one to leave open: kSettingsPath (the file
	// settingsStoreLoad() actually reads) is untouched throughout, so the
	// worst case is one lost save attempt, never a corrupted live file. The
	// stale temp file left behind is silently overwritten by the next
	// settingsStoreSave() call (removed below, then reopened for write).
	// ---------------------------------------------------------------------
	InternalFS.remove(kSettingsTmpPath);

	bool write_ok = false;
	if (settings_store_file.open(kSettingsTmpPath, FILE_O_WRITE))
	{
		size_t put = settings_store_file.write((const uint8_t *)buf, (size_t)written);
		settings_store_file.flush();
		settings_store_file.close();
		write_ok = (put == (size_t)written);
		if (!write_ok)
		{
			DEBUG_MSG("SETST", "save: short write to temp file (%u of %ld bytes)", (unsigned)put, written);
		}
	}
	else
	{
		DEBUG_MSG("SETST", "save: could not open temp file for write");
	}

	free(buf);

	if (!write_ok)
	{
		InternalFS.remove(kSettingsTmpPath);
		return false;
	}

	if (!InternalFS.rename(kSettingsTmpPath, kSettingsPath))
	{
		DEBUG_MSG("SETST", "save: rename of temp file onto live path failed");
		InternalFS.remove(kSettingsTmpPath);
		return false;
	}

	return true;
}

SettingsLoadResult settingsStoreLoad()
{
	SettingsLoadResult result;

	settings_store_file.open(kSettingsPath, FILE_O_READ);
	if (!settings_store_file)
	{
		// No file at kSettingsPath -- expected on first boot / right after a format. Not an error:
		// the caller is expected to already hold struct defaults in meshcom_settings (the same
		// discipline nrf52_flash.cpp's flash_reset() / default-construction path already follows),
		// so "no file" and "decode() found nothing to apply" leave the caller in the same state.
		result.file_found = false;
		return result;
	}
	result.file_found = true;

	// Size before the read, same reasoning as nrf52_flash.cpp's init_flash(): captured before
	// close() so it is still queryable, and here also before the read so the read length can be
	// capped to it rather than to the buffer size (avoids feeding decode() stale/garbage bytes
	// from beyond EOF if a short file were ever misread as short().
	uint32_t stored_size = settings_store_file.size();

	char *buf = (char *)malloc(kSettingsBufferCap);
	if (buf == nullptr)
	{
		DEBUG_MSG("SETST", "load: malloc(%u) failed", (unsigned)kSettingsBufferCap);
		settings_store_file.close();
		return result; // file_found=true, read_ok=false, stats default
	}

	// settingsStoreSave() never writes more than kSettingsBufferCap bytes, so a file bigger than
	// that is foreign or corrupt by construction; read at most kSettingsBufferCap and let decode()
	// make what sense it can of the prefix rather than grow the allocation to match an untrusted
	// on-disk size.
	size_t to_read = (stored_size < (uint32_t)kSettingsBufferCap) ? (size_t)stored_size : kSettingsBufferCap;

	int got = settings_store_file.read(buf, (uint16_t)to_read);
	settings_store_file.close();

	if (got < 0)
	{
		DEBUG_MSG("SETST", "load: read() failed");
		free(buf);
		return result; // file_found=true, read_ok=false, stats default
	}

	result.read_ok = true;

	// Deliberately NO layout/version check here (e.g. nothing resembling config_json.cpp's
	// CFG_IMP_ELAYOUT gate against FLASH_STRUCT_VERSION, config_json.h:157). That gate is correct
	// for configImportJson() importing a FOREIGN config file, and exactly wrong here: this is a
	// node reading its OWN settings back, which is precisely the case the D1-04 keyed-store
	// architecture (docs/BACKLOG.md, OPT-07) exists to make version-gate-free in the first place --
	// a missing key keeps meshcom_settings' existing value, an unknown key is ignored, so a field
	// reorder or an added/removed field no longer needs a layout bump to read safely. Reinstating a
	// version check on this path would silently restore the very "any struct change wipes the
	// file" failure mode (nrf52_flash.cpp:327-334, N-12) this store was built to replace. Do not
	// "fix" this comment's absence of a check.
	result.stats = settings_store::decode(settings_schema::fields(), settings_schema::fieldCount(),
										   &meshcom_settings, buf, (size_t)got);

	free(buf);
	return result;
}

#endif // NRF52_SERIES
