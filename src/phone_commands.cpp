#include "ble_phone_frame.h"
#include "ble_phone_drain.h"
#include "phone_frame.h"
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <phone_commands.h>
#include "phone_link.h"
#include "applink.h"
#include <regex_functions.h>
#include <debugconf.h>
#include <configuration.h>
#include <batt_functions.h>
#include <time.h>
#include <clock.h>
#include <rtc_functions.h>
#include <time_functions.h>
#if defined(ESP32) || defined(ESP8266)
#include "mbedtls/sha256.h"
#include "loop_breadcrumb.h"   // INS-05: loopCrumbClear() before the deliberate reboot
#else
#include "Adafruit_nRFCrypto.h"
#endif

//#include <command_functions.h>

// Create device name
extern char helper_string[256];

char textbuff_phone [MAX_MSG_LEN_PHONE] = {0};
uint8_t txt_msg_len_phone = 0;

extern int iInitDisplay;

// Client basic variables
extern uint8_t dmac[6];

// lat / lon from phone. Phone currently sends float and we cast to double. We should fix that
double d_lat = 0.0;
double d_lon = 0.0;

bool ble_busy_flag = false;

// Platform sinks for the BLE drain (src/ble_phone_drain.h): one notify per
// call, result SENT / BUSY / DOWN. Defined in esp32_main.cpp / nrf52_ble.cpp.
#if defined(ESP8266) || defined(ESP32)
BlePhoneSend esp32_write_ble(const uint8_t *buf, uint16_t len);
uint16_t esp32_ble_mtu();
#else
BlePhoneSend nrf52_write_ble(const uint8_t *buf, uint16_t len);
uint16_t nrf52_ble_mtu();
#endif

extern bool g_ble_uart_is_connected;
extern bool config_to_phone_prepare;
extern bool conffin_sent;

extern uint8_t shortVERSION();



// BLE-N1/N2: retry state per ring, the counters are the global
// g_blePhoneStats (loop_functions.cpp, shown by --info).
static BlePhoneDrainState s_phoneDrain = {0, 0, 0};
static BlePhoneDrainState s_phoneComDrain = {0, 0, 0};

static BlePhoneSend blePhoneSink(void *ctx, const uint8_t *buf, uint16_t len)
{
	(void)ctx;
#if defined(ESP8266) || defined(ESP32)
	return esp32_write_ble(buf, len);
#else
	return nrf52_write_ble(buf, len);
#endif
}

static uint16_t blePhoneMtu()
{
#if defined(ESP8266) || defined(ESP32)
	return esp32_ble_mtu();
#else
	return nrf52_ble_mtu();
#endif
}

// bBLEDEBUG lines for the drain outcomes. Kept under 64 bytes on purpose:
// Print::printf mallocs above that, and a retry happens exactly when the BLE
// stack is short of memory (printf-malloc-starves-nimble). The counters in
// g_blePhoneStats are always on; these lines only tell WHEN.
static void blePhoneDrainReport(const char *ring, BlePhoneDrain r, const BlePhoneDrainInfo &info)
{
	if(!bBLEDEBUG)
		return;

	switch(r)
	{
		case BLE_DRAIN_RETRY:
			Serial.printf("[BLE ];tx_retry;%lu;%s;try%u;len%u\n", (unsigned long)millis(), ring, (unsigned)info.fails, (unsigned)info.sendlen);
			break;
		case BLE_DRAIN_DROPPED:
			Serial.printf("[BLE ];tx_drop;%lu;%s;len%u;mtu%u\n", (unsigned long)millis(), ring, (unsigned)info.sendlen, (unsigned)info.mtu);
			break;
		case BLE_DRAIN_BAD:
			Serial.printf("[BLE ];tx_bad;%lu;%s\n", (unsigned long)millis(), ring);
			break;
		case BLE_DRAIN_SENT:
			if(info.oversize)
				Serial.printf("[BLE ];tx_trunc;%lu;len%u;mtu%u\n", (unsigned long)millis(), (unsigned)info.sendlen, (unsigned)info.mtu);
			break;
		default:
			break;
	}
}

/** AppLink sink: one phone frame as one WebSocket binary message (mtu 0 = no clamp). */
static BlePhoneSend applinkPhoneSink(void *, const uint8_t *buf, uint16_t len)
{
    return applinkSendFrame(buf, len);
}

/**
 * @brief Method to send incoming LoRa messages to BLE connected device
 * 
*/
void sendToPhone()
{
    if(ble_busy_flag)
	{
		if(bBLEDEBUG)
			Serial.println("ToPhone Busy Flag ble_busy_flag gesetzt");

        return;
	}

    ble_busy_flag = true;

    BlePhoneSinkFn sinkFn = NULL;
    uint16_t sinkMtu = 0;
    bool drainOk = false;
    switch(g_phoneLink.owner)
    {
        case PL_BLE:
            drainOk = g_ble_uart_is_connected && isPhoneReady == 1;
            sinkFn = blePhoneSink;
            sinkMtu = blePhoneMtu();
            break;
        case PL_NET:
            drainOk = applinkSessionUp() && isPhoneReady == 1;
            sinkFn = applinkPhoneSink;
            sinkMtu = 0;
            break;
        default:   // PL_NONE: frames wait like on a DOWN sink
            break;
    }

    if(drainOk)
    {
		// The drain (src/ble_phone_drain.h) peeks the ring cell, frames it
		// (blePhoneFrame(): text gets the 0x40 tag, 0x44 JSON and 0x91 MH go
		// through), sends blelen+2 bytes and pops ONLY when the stack took it.
		// On a busy stack the frame stays and is offered again at the next
		// window (BLE-N1). MAXIMUM PACKET length over BLE is 244 at MTU 247;
		// longer frames are counted, not split (BLE-N2).
		uint8_t toPhoneBuff [MAX_MSG_LEN_PHONE];
		BlePhoneDrainInfo info;

		BlePhoneDrain r = blePhoneDrainOne(&phoneRing, &s_phoneDrain, &g_blePhoneStats,
		                                   sinkFn, NULL, sinkMtu,
		                                   toPhoneBuff, sizeof(toPhoneBuff), &info);

		blePhoneDrainReport("msg", r, info);

		if(r == BLE_DRAIN_SENT)
		{
			bLED_BLUE = true;

			if(bBLEDEBUG)
			{
				if(toPhoneBuff[0] == ':' || toPhoneBuff[0] == '!' || toPhoneBuff[0] == '@')
					Serial.printf("toPhone unread:%u buff:%s lng:%i\n", (unsigned)bf_unread(&phoneRing), toPhoneBuff+7, (int)info.sendlen);
				else
					Serial.printf("toPhone unread:%u buff:%s lng:%i\n", (unsigned)bf_unread(&phoneRing), toPhoneBuff, (int)info.sendlen);
			}
		}
    }
    
    ble_busy_flag = false;
}

/**
 * @brief Method to send incoming LoRa messages to BLE connected device
 * 
*/
void sendComToPhone()
{
    if(ble_busy_flag)
	{
		if(bBLEDEBUG)
			Serial.println("ComToPhone Busy Flag ble_busy_flag gesetzt");

        return;
	}

    ble_busy_flag = true;

    BlePhoneSinkFn sinkFn = NULL;
    uint16_t sinkMtu = 0;
    bool drainOk = false;
    switch(g_phoneLink.owner)
    {
        case PL_BLE:
            drainOk = g_ble_uart_is_connected && isPhoneReady == 1;
            sinkFn = blePhoneSink;
            sinkMtu = blePhoneMtu();
            break;
        case PL_NET:
            drainOk = applinkSessionUp() && isPhoneReady == 1;
            sinkFn = applinkPhoneSink;
            sinkMtu = 0;
            break;
        default:   // PL_NONE: frames wait like on a DOWN sink
            break;
    }

    if(drainOk)
    {
		// Same drain as sendToPhone(), own retry state. The producer clamps
		// to 245, the ring takes up to 255.
		uint8_t ComToPhoneBuff [MAX_MSG_LEN_PHONE];
		BlePhoneDrainInfo info;

		BlePhoneDrain r = blePhoneDrainOne(&phoneComRing, &s_phoneComDrain, &g_blePhoneStats,
		                                   sinkFn, NULL, sinkMtu,
		                                   ComToPhoneBuff, sizeof(ComToPhoneBuff), &info);

		blePhoneDrainReport("com", r, info);

		if(r == BLE_DRAIN_SENT)
		{
			bLED_BLUE = true;

			if(bBLEDEBUG)
			{
				if(ComToPhoneBuff[0] == ':' || ComToPhoneBuff[0] == '!' || ComToPhoneBuff[0] == '@')
					Serial.printf("[BLE] <%lu> %s lng:%i\n", millis(), ComToPhoneBuff+7, (int)info.sendlen);
				else
					Serial.printf("[BLE] <%lu> %s lng:%i\n", millis(), ComToPhoneBuff, (int)info.sendlen);
			}
		}
    }
    
    ble_busy_flag = false;
}

/**
 * @brief Compute SHA-256 of the PIN formatted as zero-padded 6-digit decimal string.
 * @param pin_code  The numeric PIN (0..999999)
 * @param out_hash  32-byte output buffer
 */
static void hash_pin(uint32_t pin_code, uint8_t out_hash[32])
{
    char pin_str[8] = {0};
    snprintf(pin_str, sizeof(pin_str), "%06u", (unsigned)pin_code);
#if defined(ESP32) || defined(ESP8266)
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const unsigned char*)pin_str, 6);
    mbedtls_sha256_finish(&ctx, out_hash);
    mbedtls_sha256_free(&ctx);
#else
    nRFCrypto.begin();
    nRFCrypto_Hash hash;
    hash.begin(CRYS_HASH_SHA256_mode);
    hash.update((uint8_t*)pin_str, 6);
    hash.end(out_hash);
    nRFCrypto.end();
#endif
}

/** Phone session of `who` ended (loop task only): release the owner and reset the phone-session flags. */
void phoneSessionEnded(PhoneLinkOwner who)
{
	if(phoneLinkRelease(&g_phoneLink, who))
	{
		isPhoneReady = 0;
		config_to_phone_prepare = false;
		conffin_sent = false;
		bAckInfo = false;
	}
}

static_assert(PHONE_FRAME_MAX == MAX_MSG_LEN_PHONE, "phone_frame.h bound out of sync with the phone buffer");
static_assert(PHONE_FRAME_SSID_CAP >= sizeof(meshcom_settings.node_ssid) && PHONE_FRAME_PWD_CAP >= sizeof(meshcom_settings.node_pwd), "phone_frame.h wifi caps");

void readPhoneCommand(const uint8_t *conf_data, size_t len, PhoneLinkOwner src)
{
	/**
	 * Config Messages
     * length 1B - Msg ID 1B - Data
	 * Msg ID:
	 * 0x10 - Hello Message (followed by 0x20, 0x30)
	 * 0x20 - Timestamp from phone 
	 * 0x50 - Callsign
	 * 0x55 - Wifi SSID and PW
	 * 0x70 - Latitude
	 * 0x80 - Longitude
	 * 0x90 - Altitude
	 * 0x95 - APRS Symbols
	 * 0xA0 - Textmessage
	 * 0xF0 - Save Settings to Flash
     * Data:
     * Callsign: length Callsign 1B - Callsign
     * Latitude: 4B Float
     * Longitude: 4B Float
     * Altitude: 4B Integer
	 * 
	 * WiFi SSID and PWD:
	 * 1B - SSID Length - SSID - 1B PWD Length - PWD
	 * 
     * Position Settings from phone are: length 1B | Msg ID 1B | 4B lat/lon/alt | 1B save_settings_flag
	 * Save_flag is 0x0A for save and 0x0B for don't save
	 * If phone send periodicaly position, we don't save them.
	 * 
	 * currently we save the settings when the last config arrives which is APRS SYMBOLS - adapt is needed!
     *  */ 


	// Every read below is bounded by len, the bytes the BLE stack / AppLink actually delivered
	// (src/phone_frame.h). A header that claims more than that is dropped: reading on used to
	// run past the received data.
	PhoneFrame fr;
	phoneFrameDecode(conf_data, len, &fr);

	if(fr.status != PF_OK)
	{
		Serial.printf("[BLE] Frame dropped: %s len %u msg_len %u\n",
			fr.status == PF_TOO_SHORT ? "too short" : fr.status == PF_TOO_LONG ? "too long" : fr.status == PF_CLAIMS_MORE ? "header claims more than received" : "field past end",
			(unsigned)len, (unsigned)fr.msg_len);
		return;
	}

	bool save_setting = fr.save_flag;		//flag to save when positions from phone. config or periodic positions

	if(bBLEDEBUG)
	{
		printBuffer((uint8_t *)conf_data, fr.msg_len);
		Serial.println();
	}

	//Serial.printf("msg_type:%02x\n", msg_type);

	switch (fr.cmd)
	{
		case PFC_HELLO: {

			{

				if(bBLEDEBUG)
					Serial.println("BLE Hello Msg from phone");

				// App-layer PIN authentication.
				// If bt_code is set (> 0), the phone must send the SHA-256 hash
				// of the zero-padded 6-digit PIN string at conf_data[4..35] (32 bytes, only
				// when the frame really carries them: fr.has_hash).
				// If bt_code == 0 the original open hello is accepted.
				bool auth_ok = false;
				if(src == PL_NET)
				{
					// AppLink login already authenticated this session: no PIN hash.
					auth_ok = true;
				}
				else if(meshcom_settings.bt_code > 0 && meshcom_settings.bt_code <= 999999)
				{
					if(fr.has_hash)
					{
						uint8_t device_hash[32];
						uint8_t recv_hash[32];
						hash_pin((uint32_t)meshcom_settings.bt_code, device_hash);
						memcpy(recv_hash, fr.u.hash, 32);
						if(memcmp(recv_hash, device_hash, 32) == 0)
						{
							auth_ok = true;
							if(bBLEDEBUG)
								Serial.println("[BLE] Auth OK");
						}
						else
						{
							Serial.println("[BLE] Auth failed: wrong PIN hash");
							ble_disconnect_requested = true;
						}
					}
					else
					{
						Serial.println("[BLE] Auth rejected: PIN hash required but not provided");
						ble_disconnect_requested = true;
					}
				}
				else
				{
					// No PIN configured on device — accept as before
					if(bBLEDEBUG)
						Serial.println("[BLE] No PIN configured, accepting hello without authentication");
					auth_ok = true;
				}

				if(auth_ok)
				{
					if(src == PL_BLE && phoneLinkOwnedBy(&g_phoneLink, PL_NET))
						applinkClose(4409, "taken over by BLE");   // calls phoneSessionEnded(PL_NET)

					PhoneLinkOwner prev = phoneLinkClaim(&g_phoneLink, src);
					if(prev == PL_BLE)
						ble_disconnect_requested = true;   // drop the BLE central

					isPhoneReady = 1;
					config_to_phone_prepare = true;
					conffin_sent = false;
				}
			}

			break;
		}

		case PFC_TIME: {

			// 4B Timestamp
			uint32_t timestamp = fr.ts;

			String strDateTime = convertUNIXtoString(timestamp);
			
			if(bBLEDEBUG)
				Serial.printf("[BLE] Timestamp from phone (sec) <UTC>: %u -> %s\n", timestamp, strDateTime.c_str());


			// 2025.02.27 13:18:24

			uint16_t Year = (uint16_t)strDateTime.substring(0, 4).toInt();
			uint16_t Month = (uint16_t)strDateTime.substring(5, 7).toInt();
			uint16_t Day = (uint16_t)strDateTime.substring(8, 10).toInt();

			uint16_t Hour = (uint16_t)strDateTime.substring(11, 13).toInt();
			uint16_t Minute = (uint16_t)strDateTime.substring(14, 16).toInt();
			uint16_t Second = (uint16_t)strDateTime.substring(17).toInt();

			// set the clock
		    #if defined(ENABLE_RTC)
			if(bRTCON)
			{
				setRTCNow(Year, Month, Day, Hour, Minute, Second);
				
				DateTime utc = getRTCNow();

				DateTime now (utc + TimeSpan(meshcom_settings.node_utcoff * 60 * 60));

				meshcom_settings.node_date_year = now.year();
				meshcom_settings.node_date_month = now.month();
				meshcom_settings.node_date_day = now.day();

				meshcom_settings.node_date_hour = now.hour();
				meshcom_settings.node_date_minute = now.minute();
				meshcom_settings.node_date_second = now.second();
			}
			else
			#endif
			{
				// check valid Date & Time

				if(Year > 2020)
				{
					CLK_SRC("PHONE");
					MyClock.setCurrentTime(meshcom_settings.node_utcoff, Year, Month, Day, Hour, Minute, Second);
					bPhoneTimeValid=true;
				}
			}

			MyClock.CheckEvent();
				
			syncNodeDateFromClock();

			if (bBLEDEBUG)
			{
				Serial.printf("[BLE] Date <LT>: %02d.%02d.%04d %02d:%02d:%02d\n", meshcom_settings.node_date_day, meshcom_settings.node_date_month, meshcom_settings.node_date_year, meshcom_settings.node_date_hour, meshcom_settings.node_date_minute, meshcom_settings.node_date_second);
			}

			break;
		}

		case PFC_CALL:
		{

			DEBUG_MSG("BLE", "Callsing Setting from phone");

			// fr.u.data: NUL-terminated copy of the payload, at most 255 bytes
			const char *call_arr = (const char *)fr.u.data;


			String sVar = call_arr;
			sVar.toUpperCase();
			sVar.trim();

			// Dieselbe Normalisierung wie bei --setcall -- sonst hebt der
			// naechste Config-Schreibvorgang des Telefons die kanonische Form
			// wieder auf. Bewusst ohne checkRegexCall(): dieser Pfad hat noch
			// nie geprueft, und ihn jetzt scharf zu stellen wuerde Rufzeichen
			// abweisen, die die App bisher setzen konnte. Passt die kanonische
			// Form nicht in node_call, bleibt das Rufzeichen wie es kam.
			normalizeOwnCall(sVar);

			snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", sVar.c_str());

			snprintf(meshcom_settings.node_short, sizeof(meshcom_settings.node_short), "%s", convertCallToShort(meshcom_settings.node_call).c_str());

			//Führt zu Reconnect sendDisplayHead(false);

			#if defined NRF52_SERIES
				snprintf(helper_string, sizeof(helper_string),"%s-%02x%02x-%s", g_ble_dev_name, dmac[4], dmac[5], meshcom_settings.node_call); // Anzeige mit callsign
				
				if(bBLEDEBUG)
				{
					Serial.print("helper_string:");
					Serial.println(helper_string);
				}

				Bluefruit.setName(helper_string);
			#endif

			iInitDisplay = 99;

			break;
		}

		case PFC_LAT:
		{

			DEBUG_MSG("BLE", "Latitude Setting from phone");
			float lat_phone = fr.fval;
			
			d_lat = (double)lat_phone;
			
			nodeSetLat(d_lat); // DRY-01: range-checked, an out-of-range value from the phone is ignored

			break;
		}

		case PFC_LON:
		{

			DEBUG_MSG("BLE", "Longitude Setting from phone");

			float long_phone = fr.fval;
			d_lon = (double)long_phone;
		
			nodeSetLon(d_lon); // DRY-01: range-checked, an out-of-range value from the phone is ignored

			break;
		}

		case PFC_ALT:
		{
			int altitude = fr.ival;
			if(bBLEDEBUG)
				Serial.printf("[BLE]...Altitude from phone: %i\n", altitude);

			meshcom_settings.node_alt = altitude;

			if (!save_setting)
			{
				// send to mesh - phone sends pos perdiocaly
				DEBUG_MSG("RADIO", "Sending Pos from Phone to Mesh");
				
				posinfo_shot = true;

				pos_shot = true;
				
				wx_shot = true;
			}
			else
			{
				// save settings
				save_settings();
			}

			break;
		}

		case PFC_SYMBOL: {

			char aprs_pri_sec = fr.sym[0];
			char aprs_symbol = fr.sym[1];

			if(bBLEDEBUG)
				Serial.printf("aprs_pri_sec:%c aprs_symbol:%c\n", aprs_pri_sec, aprs_symbol);

			if(aprs_pri_sec == 0x2f || aprs_pri_sec == 0x5c)
			{

				// Variablen entsprechend setzen beim APRS Encode
				meshcom_settings.node_symid = aprs_pri_sec;
				meshcom_settings.node_symcd = aprs_symbol;

			}
			break;
		}

		case PFC_TEXT: {
			// length 1B - Msg ID 1B - Text
			// (msg_len < 2 never gets here: the decoder ignores it, avoiding the unsigned underflow)

			txt_msg_len_phone = fr.dlen;	// now zero escape for lora TX

			// Spin-wait removed: readPhoneCommand now runs in Main Loop,
			// no cross-core conflict with sendToPhone() possible

			if (bBLEDEBUG)
				Serial.printf("Text from phone: %s\n", fr.u.data);

			// kopieren der message in buffer fuer main
			int iposn=0;
			if(!fr.dash)
			{
				textbuff_phone[0] = ':';
				iposn=1;
			}
			memcpy(textbuff_phone+iposn, fr.u.data, txt_msg_len_phone);
			textbuff_phone[txt_msg_len_phone+iposn]=0x00;

			// flag für main neue msg von phone
			hasMsgFromPhone = true;
			break;
		}

		case PFC_WIFI: {
			// 1B - SSID Length - SSID - 1B PWD Length - PWD

			DEBUG_MSG("BLE", "Wifi Setting from phone");

			// the decoder bounded ssid_len and pwd_len against the frame length and cut both
			// copies at the capacity of meshcom_settings.node_ssid/node_pwd (NUL-terminated)
			{
				String s_SSID = fr.u.wifi.ssid;
				String s_PWD = fr.u.wifi.pwd;

				snprintf(meshcom_settings.node_ssid, sizeof(meshcom_settings.node_ssid),"%s", s_SSID.c_str());
				snprintf(meshcom_settings.node_pwd, sizeof(meshcom_settings.node_pwd),"%s", s_PWD.c_str());

				if(bBLEDEBUG)
					Serial.println("Wifi Setting from phone set");

				// Node will reset after saving settings. Settings back are coming on ble reconnect.

			}
			break;
		}

		case PFC_SAVE_RESET: {
			
			if(bBLEDEBUG)
				Serial.println("Save Settings");
			
			//Save Settings

			save_settings();
			//delay(1000);
			
			// send config back to phone
			//sendConfigToPhone();	// config data comes now via JSONs from main loop and commandfunctions

			// reset node
			delay(2000);
			#if defined NRF52_SERIES
				NVIC_SystemReset();
			#else
				loopCrumbClear();   // INS-05: deliberate reboot, no LAST_LOOP_SECTION at the next boot
				ESP.restart();
			#endif
		}

		default:
			break;	// PFC_NONE: unknown type or an ignored frame
	}
}
