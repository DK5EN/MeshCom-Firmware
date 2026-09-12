// In-memory fake filesystem for the native nRF52 settings cutover suite
// (test_nrf52_settings_paths). Stands in for Adafruit's real
// Adafruit_LittleFS.h (part of the nRF52 Arduino BSP, not available/
// buildable on a native host) -- shadowed via plain -I ordering, since both
// src/nrf52/nrf52_flash.cpp and src/nrf52/settings_store_nrf52.cpp reach it
// through an ANGLE-BRACKET `#include <Adafruit_LittleFS.h>` (no
// same-directory quoted-include collision the way "WisBlock-API.h" has).
//
// This is deliberately not a byte-for-byte mirror of Adafruit's class shape
// (that's a hardware BSP header, not something worth reproducing exactly) --
// it is a fresh, from-scratch fake that reproduces exactly the semantics the
// product code under test (nrf52_flash.cpp / settings_store_nrf52.cpp)
// depends on, PLUS the fidelity the test brief calls for: a missing file, a
// falsy open() on it, size(), partial/short reads and short writes (test-
// controlled, one-shot knobs), remove(), rename() replacing an existing
// destination, and format() erasing everything.
//
// One global FakeFsState (g_fake_fs) backs both File instances the product
// code constructs (nrf52_flash.cpp's `lora_file` and settings_store_nrf52's
// `settings_store_file`) -- exactly like real flash, there is one underlying
// storage device no matter how many File handles reference it.
#pragma once

#ifndef NATIVE_BUILD
#error "test_nrf52_settings_paths/stubs/Adafruit_LittleFS.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// File open mode constants (Adafruit_LittleFS.h's real spelling; the product
// code passes these as the second argument to File::open()).
enum
{
	FILE_O_READ = 0,
	FILE_O_WRITE = 1,
};

// ---------------------------------------------------------------------------
// The fake device: one process-wide table of path -> bytes, plus counters and
// one-shot test knobs. Reset explicitly by the test's setUp() between cases
// (see test_nrf52_settings_paths.cpp) -- nothing here resets itself.
// ---------------------------------------------------------------------------
struct FakeFsState
{
	struct Entry
	{
		std::vector<uint8_t> data;
	};

	std::map<std::string, Entry> files;

	// Call counters -- e.g. the skip-if-unchanged test proves "no second
	// write happened" by reading open_write_calls before/after, not by
	// inspecting settingsStoreSave()'s return value.
	int format_calls = 0;
	int rename_calls = 0;
	int remove_calls = 0;
	int open_write_calls = 0;
	int open_read_calls = 0;

	// One-shot knobs a test arms immediately before the call it wants to
	// affect; each knob consumes itself (resets to -1) the moment it is
	// used, so it never leaks into a later, unrelated read/write in the same
	// test.
	long force_short_read = -1;  // next read() returns at most this many bytes
	long force_short_write = -1; // next write() returns at most this many bytes

	// The whole point of the temp-file-then-rename atomicity claim
	// (settings_store_nrf52.cpp) is that the LIVE path holds its OLD content
	// right up until the moment rename() swaps the temp file onto it.
	// rename() below captures exactly that: what bytes the destination path
	// held immediately before being replaced (and whether it existed at
	// all). A test reads this back after calling the save path to prove the
	// live path was never written to directly.
	bool last_rename_had_dest = false;
	std::vector<uint8_t> last_rename_dest_previous_content;

	void reset()
	{
		files.clear();
		format_calls = rename_calls = remove_calls = 0;
		open_write_calls = open_read_calls = 0;
		force_short_read = force_short_write = -1;
		last_rename_had_dest = false;
		last_rename_dest_previous_content.clear();
	}

	// ---- test-only helpers (never called from product code) --------------

	void seed(const char *path, const void *data, size_t len)
	{
		const uint8_t *p = static_cast<const uint8_t *>(data);
		files[path] = Entry{std::vector<uint8_t>(p, p + len)};
	}

	bool exists(const char *path) const { return files.count(path) != 0; }

	// Returns nullptr if the path has no file.
	const std::vector<uint8_t> *peek(const char *path) const
	{
		auto it = files.find(path);
		return it == files.end() ? nullptr : &it->second.data;
	}
};

// C++17 inline variable: one definition shared across every translation
// unit that includes this header (same idiom test/support/Arduino.h already
// uses for its Serial/millis globals), so no separate stub .cpp is needed
// just to house this.
inline FakeFsState g_fake_fs;

// ---------------------------------------------------------------------------
// The fake filesystem object itself (InternalFS is an instance of this, see
// InternalFileSystem.h).
// ---------------------------------------------------------------------------
class Adafruit_LittleFS
{
public:
	bool begin() { return true; }

	bool format()
	{
		g_fake_fs.files.clear();
		g_fake_fs.format_calls++;
		return true;
	}

	bool rename(char const *from, char const *to)
	{
		auto it = g_fake_fs.files.find(from);
		if (it == g_fake_fs.files.end())
			return false;

		auto dst = g_fake_fs.files.find(to);
		g_fake_fs.last_rename_had_dest = (dst != g_fake_fs.files.end());
		g_fake_fs.last_rename_dest_previous_content =
			g_fake_fs.last_rename_had_dest ? dst->second.data : std::vector<uint8_t>();

		g_fake_fs.files[to] = it->second; // replaces an existing destination
		g_fake_fs.files.erase(it);
		g_fake_fs.rename_calls++;
		return true;
	}

	bool remove(char const *filepath)
	{
		auto it = g_fake_fs.files.find(filepath);
		if (it == g_fake_fs.files.end())
			return false;
		g_fake_fs.files.erase(it);
		g_fake_fs.remove_calls++;
		return true;
	}
};

namespace Adafruit_LittleFS_Namespace
{

class File
{
public:
	explicit File(Adafruit_LittleFS &fs) : fs_(&fs) {}

	bool open(char const *filepath, uint8_t mode)
	{
		path_ = filepath;
		pos_ = 0;
		mode_ = mode;

		if (mode == FILE_O_WRITE)
		{
			g_fake_fs.open_write_calls++;
			// Real LittleFS truncates on write-open; every byte the caller
			// writes lands only under `path_` (the temp path, for the
			// settings-store save sequence) -- the destination of a later
			// rename() is untouched until that call.
			g_fake_fs.files[path_] = FakeFsState::Entry{};
			is_open_ = true;
			return true;
		}

		g_fake_fs.open_read_calls++;
		auto it = g_fake_fs.files.find(path_);
		if (it == g_fake_fs.files.end())
		{
			is_open_ = false;
			return false; // missing file: falsy open(), matches the brief
		}
		is_open_ = true;
		return true;
	}

	// `if (!lora_file)` / `if (!settings_store_file)` -- both real call
	// sites test truthiness rather than open()'s return value.
	operator bool() const { return is_open_; }

	uint32_t size() const
	{
		auto it = g_fake_fs.files.find(path_);
		return it == g_fake_fs.files.end() ? 0 : static_cast<uint32_t>(it->second.data.size());
	}

	int read(void *buf, size_t len)
	{
		if (!is_open_)
			return -1;
		auto it = g_fake_fs.files.find(path_);
		if (it == g_fake_fs.files.end())
			return -1;

		const auto &data = it->second.data;
		size_t avail = (pos_ < data.size()) ? (data.size() - pos_) : 0;
		size_t want = len < avail ? len : avail;

		if (g_fake_fs.force_short_read >= 0)
		{
			size_t cap = static_cast<size_t>(g_fake_fs.force_short_read);
			if (want > cap)
				want = cap;
			g_fake_fs.force_short_read = -1; // one-shot
		}

		if (want > 0)
			memcpy(buf, data.data() + pos_, want);
		pos_ += want;
		return static_cast<int>(want);
	}

	size_t write(const void *buf, size_t len)
	{
		if (!is_open_ || mode_ != FILE_O_WRITE)
			return 0;

		size_t want = len;
		if (g_fake_fs.force_short_write >= 0)
		{
			size_t cap = static_cast<size_t>(g_fake_fs.force_short_write);
			if (want > cap)
				want = cap;
			g_fake_fs.force_short_write = -1; // one-shot
		}

		auto &entry = g_fake_fs.files[path_];
		const uint8_t *p = static_cast<const uint8_t *>(buf);
		entry.data.insert(entry.data.end(), p, p + want);
		pos_ += want;
		return want;
	}

	void flush() {}

	void close() { is_open_ = false; }

private:
	// Kept only for constructor-signature parity with the real File(fs&) --
	// there is one process-wide g_fake_fs regardless of which instance
	// constructed a given File, exactly like the real singleton InternalFS.
	[[maybe_unused]] Adafruit_LittleFS *fs_;
	std::string path_;
	size_t pos_ = 0;
	uint8_t mode_ = FILE_O_READ;
	bool is_open_ = false;
};

} // namespace Adafruit_LittleFS_Namespace
