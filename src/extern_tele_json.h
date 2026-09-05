#ifndef _EXTERN_TELE_JSON_H_
#define _EXTERN_TELE_JSON_H_

#include <stddef.h>
#include <ArduinoJson.h>

// TLM-04: the EXTUDP "tele" datagram, built here rather than inline in
// sendExtern() (extudp_functions.cpp) so the key contract is native-testable
// (test/test_extern_tele_json). Same pattern as extern_notice_json.h.
//
// Both shapes carry the same keys with the same physical meaning:
//   qfe  station pressure in hPa (APRS /P=)
//   qnh  pressure reduced to mean sea level in hPa (APRS /Q=)
// The "lora" shape additionally carries pressure_alt, the barometric
// pressure altitude in metres against 1013.25 hPa (APRS /F=). Before TLM-04
// the /F= altitude was written under "qfe", so a relayed BME680 node showed
// e.g. 191 "hPa" on a dashboard.
//
// Rueckgabe: Anzahl geschriebener Bytes; die Schranke ist die Puffergroesse,
// nicht measureJson() (JSN-01, siehe ble_json_frame.h).

// "node": the gateway's own sensor values.
static inline size_t externTeleJsonNode(char *out, size_t out_len,
                                        const char *src,
                                        float temp1, float temp2, float hum,
                                        float qfe, float qnh,
                                        float gas, float co2)
{
    if(out == nullptr || out_len == 0)
        return 0;

    JsonDocument ctJson;

    ctJson["src_type"] = "node";
    ctJson["type"] = "tele";
    ctJson["src"] = src;
    ctJson["temp1"] = temp1;
    ctJson["temp2"] = temp2;
    ctJson["hum"] = hum;
    ctJson["qfe"] = qfe;
    ctJson["qnh"] = qnh;
    ctJson["gas"] = gas;
    ctJson["co2"] = co2;

    return serializeJson(ctJson, out, out_len);
}

// "lora": a relayed node's values as parsed from its position frame.
static inline size_t externTeleJsonLora(char *out, size_t out_len,
                                        const char *src, int batt,
                                        float temp1, float temp2, float hum,
                                        float qfe, float qnh,
                                        int pressure_alt,
                                        float gas, float co2)
{
    if(out == nullptr || out_len == 0)
        return 0;

    JsonDocument ctJson;

    ctJson["src_type"] = "lora";
    ctJson["type"] = "tele";
    ctJson["src"] = src;
    ctJson["batt"] = batt;
    ctJson["temp1"] = temp1;
    ctJson["temp2"] = temp2;
    ctJson["hum"] = hum;
    ctJson["qfe"] = qfe;
    ctJson["qnh"] = qnh;
    ctJson["pressure_alt"] = pressure_alt;
    ctJson["gas"] = gas;
    ctJson["co2"] = co2;

    return serializeJson(ctJson, out, out_len);
}

#endif // _EXTERN_TELE_JSON_H_
