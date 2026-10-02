// Targeted, one-shot malloc() interposition for the allocation-failure paths
// in settings_store_nrf52.cpp's settingsStoreSave()/settingsStoreLoad()
// (both `malloc(kSettingsBufferCap)`, kSettingsBufferCap == 4096, an
// internal, non-exported constant -- there is no injection seam in the
// product code to fail this any other way).
//
// This overrides the process's real malloc()/free() by simply providing
// strong definitions of both symbols in this translation unit, linked into
// the same executable as every other object file in this suite -- the
// linker resolves every caller's `malloc`/`free` reference to THESE
// definitions rather than pulling libc's from its archive, which already
// satisfies the reference. Verified empirically while building this suite:
// std::vector/std::map/std::string (used throughout FakeFsState and Unity's
// own internals) keep working normally through the real-malloc passthrough
// below, since mc_arm_malloc_failure() is armed only immediately before the
// single call site a test wants to fail, for exactly one allocation of
// exactly the size the test names, and disarms itself the instant it fires
// (or the instant a differently-sized allocation passes through it) --
// nothing else in the process ever observes a null from this.
#include <cstddef>
#include <dlfcn.h>

namespace
{
bool g_armed = false;
size_t g_target_size = 0;
bool g_fired = false;

typedef void *(*malloc_fn)(size_t);

malloc_fn real_malloc()
{
	static malloc_fn fn = reinterpret_cast<malloc_fn>(dlsym(RTLD_NEXT, "malloc"));
	return fn;
}
} // namespace

// Test-facing control surface (declared extern "C" so the test .cpp can call
// it without needing this header).
// Arms the next allocation of AT LEAST `min_size` bytes to fail.
//
// This used to match on an EXACT size, and that was a defect in the harness,
// not a detail. The tests armed it with the literal 4096 because that was
// kSettingsBufferCap at the time. When the cap was raised to 8192,
// malloc(8192) stopped matching malloc(4096), the injected failure silently
// never fired, and both "allocation failure" cases went green while testing
// nothing. A fault injector that quietly stops injecting is worse than none,
// so the match is a lower bound now, and mc_malloc_failure_fired() lets a
// test assert the fault actually happened.
extern "C" void mc_arm_malloc_failure(size_t min_size)
{
	g_armed = true;
	g_fired = false;
	g_target_size = min_size;
}

// True if the armed failure actually fired since arming. A fault-injection
// test that does not assert this cannot distinguish "the code handled the
// failure" from "the failure never happened".
extern "C" bool mc_malloc_failure_fired()
{
	return g_fired;
}

extern "C" void mc_disarm_malloc_failure()
{
	g_armed = false;
	g_fired = false;
	g_target_size = 0;
}

extern "C" void *malloc(size_t n)
{
	if (g_armed && n >= g_target_size)
	{
		g_armed = false; // one-shot: only the next matching call fails
		g_fired = true;
		return nullptr;
	}
	return real_malloc()(n);
}
