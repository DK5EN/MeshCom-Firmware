/**
 * @file ble_settings_stage.h
 * @brief Hand-over of a BLE settings write from the BLE callback task to the
 * loop task (CONC-17), without a snapshot or a converted copy.
 *
 * settings_rx_callback() (nrf52_ble.cpp) stages the wire image with
 * stageBleSettingsV1(); applyPendingBleSettings() calls
 * tryApplyBleSettingsV1Stage() inside its own critical section, which converts
 * the image straight onto meshcom_settings. Converting in place writes only
 * the v1 fields, so node_msgid (never written by bleSettingsFromV1()) keeps
 * its live value.
 *
 * Header-only and free of FreeRTOS/Arduino includes so that the native suite
 * (test/test_ble_settings_v1) can drive it; the critical section and the
 * yield live in the caller.
 */
#ifndef BLE_SETTINGS_STAGE_H
#define BLE_SETTINGS_STAGE_H

#include <cstdint>
#include <cstring>

#include "ble_settings_v1.h"

/**
 * Zero-initialised storage for one s_ble_settings_v1 image. The struct's
 * non-zero member initialisers would put a plain static in .data, costing its
 * size in RAM and again in flash; the constexpr zero-fill of `raw` puts it in
 * .bss. Safe because every user writes `img` fully before reading it
 * (bleSettingsToV1() sets every v1 member, stageBleSettingsV1() copies the
 * whole image).
 */
union BleSettingsV1Storage
{
	constexpr BleSettingsV1Storage() : raw{} {}
	s_ble_settings_v1 img;
	uint8_t raw[sizeof(s_ble_settings_v1)];
};

/**
 * One staged write plus its sequence counter (a seqlock with a single
 * writer). The writer makes `seq` odd before touching `buf` and even after,
 * with barriers on both sides. The reader runs inside a critical section, so
 * the writer cannot progress while it reads; an odd `seq` means a write was
 * paused mid-copy and the reader must back off.
 */
struct BleSettingsV1Stage
{
	volatile uint32_t seq = 0;
	BleSettingsV1Storage buf;
};

/**
 * Stage a validated (length + markers) wire image. BLE callback task only;
 * runs outside any critical section, the counter protects the reader.
 */
inline void stageBleSettingsV1(BleSettingsV1Stage &stage, const s_ble_settings_v1 &image)
{
	stage.seq++; // now odd: a concurrent reader must back off
	__sync_synchronize();
	memcpy(&stage.buf.img, &image, sizeof(s_ble_settings_v1));
	__sync_synchronize();
	stage.seq++; // now even again: the image is complete and consistent
}

/** Result of a single tryApplyBleSettingsV1Stage() attempt. */
enum class BleSettingsV1ApplyResult
{
	None,	 // nothing staged since the last successful apply
	Busy,	 // a write is in progress (seq odd) -- caller should back off and retry
	Applied	 // the staged image was converted onto `live`
};

/**
 * QA-NET-02: the settings characteristic is SECMODE_OPEN, so the secrets are
 * not put on the wire. nRF52 only; ESP32 has no settings characteristic.
 * Masked read value is empty (never "***", a read->write round trip would
 * store it). bt_code: 0 = no PIN stays 0, a set PIN reads as -1 (outside
 * 0..999999); -1 on write keeps the stored PIN, 0 still disables it.
 * Applied after bleSettingsToV1(), not inside it: the flash path shares that
 * function and must keep the real values.
 */
#define BLE_SETTINGS_V1_BTCODE_MASKED (-1)

inline void bleSettingsV1MaskSecrets(s_ble_settings_v1 &img)
{
	memset(img.node_passwd, 0, sizeof(img.node_passwd));
	memset(img.node_webpwd, 0, sizeof(img.node_webpwd));
	memset(img.node_pwd, 0, sizeof(img.node_pwd));
	if (img.bt_code != 0)
		img.bt_code = BLE_SETTINGS_V1_BTCODE_MASKED;
}

// Empty or leading space/NUL counts as unset, same test as maskSecret().
inline bool bleSettingsV1SecretMasked(const char *s)
{
	return s[0] == 0x00 || s[0] == ' ';
}

/**
 * Apply the staged image onto `live` if there is a new, complete one. Takes
 * no lock itself: the caller wraps this call in taskENTER_CRITICAL() /
 * taskEXIT_CRITICAL(), which keeps the callback task out while it runs.
 * Converts directly onto `live`, so members bleSettingsFromV1() does not
 * write (node_msgid) keep their live values.
 */
inline BleSettingsV1ApplyResult tryApplyBleSettingsV1Stage(const BleSettingsV1Stage &stage,
															uint32_t *appliedSeq,
															s_meshcom_settings &live)
{
	uint32_t seq = stage.seq;
	if (seq & 1u)
		return BleSettingsV1ApplyResult::Busy;
	if (seq == *appliedSeq)
		return BleSettingsV1ApplyResult::None;

	// A masked secret field keeps the stored value (QA-NET-02).
	const s_ble_settings_v1 &in = stage.buf.img;
	char keepPasswd[sizeof(live.node_passwd)];
	char keepWebpwd[sizeof(live.node_webpwd)];
	char keepPwd[sizeof(live.node_pwd)];
	memcpy(keepPasswd, live.node_passwd, sizeof(keepPasswd));
	memcpy(keepWebpwd, live.node_webpwd, sizeof(keepWebpwd));
	memcpy(keepPwd, live.node_pwd, sizeof(keepPwd));
	const int keepBtCode = live.bt_code;

	bleSettingsFromV1(in, live);

	if (bleSettingsV1SecretMasked(in.node_passwd))
		memcpy(live.node_passwd, keepPasswd, sizeof(keepPasswd));
	if (bleSettingsV1SecretMasked(in.node_webpwd))
		memcpy(live.node_webpwd, keepWebpwd, sizeof(keepWebpwd));
	if (bleSettingsV1SecretMasked(in.node_pwd))
		memcpy(live.node_pwd, keepPwd, sizeof(keepPwd));
	if (in.bt_code == BLE_SETTINGS_V1_BTCODE_MASKED)
		live.bt_code = keepBtCode;
	*appliedSeq = seq;
	return BleSettingsV1ApplyResult::Applied;
}

#endif // BLE_SETTINGS_STAGE_H
