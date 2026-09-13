// Stand-in for src/nrf52/WisBlock-API.h, for the native test_ble_settings_v1
// suite (BLE settings v1 wire-format conversion).
//
// WHY THIS FILE IS SHAPED THE WAY IT IS
// ---------------------------------------------------------------------------
// src/nrf52/ble_settings_v1.h -- the header under test -- does a QUOTED
// include of "WisBlock-API.h", and it lives in src/nrf52/, the SAME
// directory as the real header. For a quoted include, the compiler searches
// the including file's own directory FIRST, before any -I path is even
// consulted (same fact test_nrf52_settings_paths's stub documents at
// length). No -I ordering can shadow "WisBlock-API.h" for that file; the
// real header WILL be opened.
//
// The real header pulls in <bluefruit.h>, <LoRaWan-Arduino.h>, FreeRTOS
// handle types, BLE characteristic/service types -- none of it buildable on
// a native host, and none of it needed by the conversion functions under
// test. The fix is the same "predefine the include guard" technique
// test_nrf52_settings_paths already uses: this file defines the SAME guard
// macro (SX126x_API_H) and is FORCE-INCLUDED ahead of every translation
// unit in this suite's env (see [env:native_ble_settings_v1] in
// platformio.ini and force_include_stub.py in this directory). By the time
// ble_settings_v1.h's own `#include "WisBlock-API.h"` line is reached, the
// guard is already defined, so the real file's entire body is skipped.
//
// SCOPE: only the struct is reproduced -- nothing else the real header
// declares (BLE/WiFi/LoRaWAN surface, flash function prototypes, globals)
// is needed by src/nrf52/ble_settings_v1.h/.cpp, which touch only the
// s_meshcom_settings TYPE, never the global `meshcom_settings` instance or
// any flash/BLE function.
//
// THIS STRUCT MUST STAY A FULL, FIELD-FOR-FIELD COPY of the real
// s_meshcom_settings (src/nrf52/WisBlock-API.h): same order, same types,
// same array bounds, same default member initialisers. The suite's golden
// byte image and every static_assert in ble_settings_v1.h check the REAL
// struct's contract; a stub that has drifted from it would make this suite
// pass while proving nothing about the shipped layout. There is no
// automated drift check registered for this stub (test/golden/
// twin_stub_lint.py's PAIRS list is outside this brief's file set) -- diff
// this struct body against src/nrf52/WisBlock-API.h by hand if either one
// changes.
#ifndef SX126x_API_H
#define SX126x_API_H

#ifndef NATIVE_BUILD
#error "test_ble_settings_v1/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <Arduino.h> // boolean (test/support/Arduino.h)
#include <cstdint>

#define MESHCOM_DATA_MARKER 0x55

// ---------------------------------------------------------------------------
// s_meshcom_settings -- verbatim field-for-field copy of the real struct
// (src/nrf52/WisBlock-API.h), same order, same default member initialisers,
// as of commit 64ba5774 (the commit ble_settings_v1.h freezes against).
// ---------------------------------------------------------------------------
struct s_meshcom_settings
{
	uint8_t valid_mark_1 = 0xAA;				// Just a marker for the Flash
	uint8_t valid_mark_2 = MESHCOM_DATA_MARKER; // Just a marker for the Flash

	// OTAA Device EUI MSB
	uint8_t node_device_eui[8] = {0x00, 0x0D, 0x75, 0xE6, 0x56, 0x4D, 0xC1, 0xF3};

	char node_call[10] = {0x58, 0x58, 0x30, 0x58, 0x58, 0x58, 0x2D, 0x30, 0x30, 0x00};
	char node_short[6] = {0x58, 0x58, 0x58, 0x34, 0x30, 0x00};

	double node_lat = 0.0;
	char node_lat_c = {' '};
	double node_lon = 0.0;
	char node_lon_c = {' '};
	int	  node_alt = 0;
	char  node_symid = '/';
	char  node_symcd = '#';

	int node_date_year = 0;
	int node_date_month = 0;
	int node_date_day = 0;

	int node_date_hour = 0;
	int node_date_minute = 0;
	int node_date_second = 0;
	int node_date_hundredths = 0;

	unsigned long node_age = 0;

	float node_temp = 0;
	float node_hum = 0;
	float node_press = 0;

	char node_ossid[40] = {0};
	char node_opwd[40] = {0};

	// Default is off
	uint32_t send_repeat_time = 0;

	bool auto_join = false;

	int node_hamnet_only = 0;

	int node_sset = 0;

	float node_maxv = 4.200;

	char node_extern[40] = {0};

	int node_msgid = 0;
	int node_ackid = 0;

	int node_power = -20;
	float node_freq = 0;
	float node_bw = 0;
	int node_sf = 0;
	int node_cr = 0;

	char node_atxt[40] = {0};

	int node_sset2 = 0;
	int node_owgpio = 16;

	float node_temp2 = 0;

	float node_utcoff = 0;

	// BME680
	float node_gas_res = 0;

	// CMCU-811
	float node_co2 = 0;

	// MCP23017
	int node_mcp17io = 0;
	char node_mcp17t[16][16] = {0};
	int node_mcp17out = 0;
	int node_mcp17in = 0;

	// GC Fields
	int node_gcb[6] = {0};

	// internatioal fields
	int node_country = 0;

	float node_track_freq = 0;
	int node_preamplebits = 0;

	int node_ss_rx_pin = 0;
	int node_ss_tx_pin = 0;
	int node_ss_baud = 0;

	int node_postime = 0;

	char node_passwd[15] = {0};

	int node_sset3 = 0;

	int bt_code = 0;

	int node_button_pin = 0;

	char node_ownip[20] = {0};
	char node_owngw[20] = {0};
	char node_ownms[20] = {0};

	char node_name[20] = {0};

	char node_webpwd[20] = {0};

	char node_ssid[33] = {0};
	char node_pwd[64] = {0};

	int node_analog_pin = 99;
	float node_analog_faktor = 1.0;

	char node_parm[50] = {0};
	char node_unit[50] = {0};
	char node_format[50] = {0};
	char node_eqns[50] = {0};
	char node_values[50] = {0};

	int node_parm_time = 15;

	int node_wifi_power = 60;
	char node_lora_call[10] = {0x00};

	float node_analog_alpha = 0.0;
	float node_analog_slope = 0.0;
	float node_analog_offset = 0.0;
	float node_analog_atten = 0.0;

	char node_gwsrv[3] = {0x00};

	float node_tempi_off = 0.0;
	float node_tempo_off = 0.0;

	float node_shunt = 0.002;
	float node_imax = 20.0;
	int node_isamp = 7;

	char node_owndns[20] = {0};

	int node_contrast = 255;
	int node_fversion = 1;

	char node_ownntp[40] = {0};

	int node_mversion = 0;
	char node_fwversion[8] = {0};

	uint32_t node_gpsbaud = 38400;   // D1-04: one spelling on both platforms; CFG_U32 range 1200..921600 needs 32 bit

	int node_cleanflash = 0;

	int node_netmode = 0;

	int node_gpsdebug = 0;

	int node_relay = 0x0000;

	char node_via[40] = {0};

	int node_sset4 = 0x0002;

	char node_aprsmc[10] = {0};

	int node_pingtime = 0;
	char node_pingcall[10] = {0};
	int node_pingmax = 0;

	//////////////////////////////////////////////////////////////////////////////////////////////
	// nicht im Flash
	float node_specstart = 432.0;
	float node_specend = 434.0;
	float node_specstep = 0.025;
	int node_specsamples = 2048;

	float node_analog_batt_faktor = 0.0;


	int node_press_alt = 0;
	float node_press_asl = 0;
	float node_vbus = 0;
	float node_vshunt = 0;
	float node_vcurrent = 0;
	float node_vpower = 0;

	char node_ip[40] = {0};
	char node_dns[40] = {0};
	char node_gw[40] = {0};
	char node_subnet[40] = {0};
	bool node_hasIPaddress = false;
	unsigned long node_last_upd_timer = 0;

	int max_hop_text = 0;
	int max_hop_pos = 0;

	// 9999-99-99 00:00:00
	char node_update[20] = {0};

	char node_parm_1[100] = {0};
	char node_parm_t[150] = {0};
	char node_parm_id[100] = {0};

	float node_ntctemp = 0.0;
	bool node_fanon = false;

	int node_pingcount = 0;
	unsigned long node_pingduration = 0;
};

#endif // SX126x_API_H
