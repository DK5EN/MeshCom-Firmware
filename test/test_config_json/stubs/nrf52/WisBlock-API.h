// Minimal-Shim von nrf52/WisBlock-API.h fuer den nativen Testbuild
// (env:native_config, CS-03 / BACKLOG.md #3.8h).
//
// config_json.cpp zieht auf Nicht-ESP32-Plattformen nrf52/WisBlock-API.h
// herein (BLE-, SoftDevice- und SDK-Header), das nativ nicht kompiliert.
// Gebraucht wird davon nur struct s_meshcom_settings -- und von der genau
// die Felder, die die Feldtabelle in config_json.cpp im nRF52-Zweig
// adressiert. -I test/test_config_json/stubs steht in platformio.ini vor
// -I test/support, dieser Shim ersetzt also den dortigen fuer env:native_config.
//
// Typen und Defaults sind 1:1 aus src/nrf52/WisBlock-API.h gespiegelt --
// insbesondere node_gpsbaud als 'unsigned int' (auf dem ESP32 'unsigned long')
// und send_repeat_time als uint32_t; config_json.cpp static_assertet, dass das
// 4 Byte breit bleibt.
//
// Absichtlich KEINE weiteren Felder: greift die Tabelle kuenftig auf mehr zu,
// soll der Compiler das hier sichtbar machen, statt dass ein stillschweigend
// abweichender Shim falsche Sicherheit gibt. Insbesondere fehlen alle Felder,
// die der Export bewusst NICHT traegt (node_fversion, node_cflash, die
// T-Deck-Felder, die "nicht im Flash"-Felder) -- taucht eines davon in der
// Tabelle auf, bricht der native Build.
#pragma once

#ifndef NATIVE_BUILD
#error "test/test_config_json/stubs/nrf52/WisBlock-API.h darf nur im nativen Testbuild verwendet werden"
#endif

#include <stdint.h>

struct s_meshcom_settings
{
    char   node_call[10] = {'X', 'X', '0', 'X', 'X', 'X', '-', '0', '0', 0x00};
    char   node_short[6] = {'X', 'X', 'X', '4', '0', 0x00};

    double node_lat = 0.0;
    char   node_lat_c = ' ';
    double node_lon = 0.0;
    char   node_lon_c = ' ';
    int    node_alt = 0;
    char   node_symid = '/';
    char   node_symcd = '#';

    float  node_temp = 0;
    float  node_hum = 0;
    float  node_press = 0;

    char   node_ossid[40] = {0};
    char   node_opwd[40] = {0};

    uint32_t send_repeat_time = 0;
    bool     auto_join = false;

    int    node_hamnet_only = 0;
    int    node_sset = 0;
    float  node_maxv = 4.200f;
    char   node_extern[40] = {0};

    int    node_msgid = 0;
    int    node_ackid = 0;

    int    node_power = -20;
    float  node_freq = 0;
    float  node_bw = 0;
    int    node_sf = 0;
    int    node_cr = 0;

    char   node_atxt[40] = {0};

    int    node_sset2 = 0;
    int    node_owgpio = 16;

    float  node_temp2 = 0;
    float  node_utcoff = 0;
    float  node_gas_res = 0;
    float  node_co2 = 0;

    int    node_mcp17io = 0;
    char   node_mcp17t[16][16] = {{0}};
    int    node_mcp17out = 0;
    int    node_mcp17in = 0;

    int    node_gcb[6] = {0};

    int    node_country = 0;

    float  node_track_freq = 0;
    int    node_preamplebits = 0;

    int    node_ss_rx_pin = 0;
    int    node_ss_tx_pin = 0;
    int    node_ss_baud = 0;

    int    node_postime = 0;

    char   node_passwd[15] = {0};

    int    node_sset3 = 0;
    int    bt_code = 0;
    int    node_button_pin = 0;

    char   node_ownip[20] = {0};
    char   node_owngw[20] = {0};
    char   node_ownms[20] = {0};
    char   node_name[20] = {0};
    char   node_webpwd[20] = {0};
    char   node_ssid[33] = {0};
    char   node_pwd[64] = {0};

    int    node_analog_pin = 99;
    float  node_analog_faktor = 1.0f;

    char   node_parm[50] = {0};
    char   node_unit[50] = {0};
    char   node_format[50] = {0};
    char   node_eqns[50] = {0};
    char   node_values[50] = {0};

    int    node_parm_time = 15;

    int    node_wifi_power = 60;
    char   node_lora_call[10] = {0};

    float  node_analog_alpha = 0.0f;
    float  node_analog_slope = 0.0f;
    float  node_analog_offset = 0.0f;
    float  node_analog_atten = 0.0f;

    char   node_gwsrv[3] = {0};

    float  node_tempi_off = 0.0f;
    float  node_tempo_off = 0.0f;

    float  node_shunt = 0.002f;
    float  node_imax = 20.0f;
    int    node_isamp = 7;

    char   node_owndns[20] = {0};

    int    node_contrast = 255;

    char   node_ownntp[40] = {0};

    unsigned int node_gpsbaud = 38400;

    int    node_netmode = 0;
    int    node_gpsdebug = 0;
    int    node_relay = 0x0000;

    char   node_via[40] = {0};

    int    node_sset4 = 0x0002;

    char   node_aprsmc[10] = {0};

    int    node_pingtime = 0;
    char   node_pingcall[10] = {0};
    int    node_pingmax = 0;

    // max_hop_text ist auf beiden Plattformen persistent (CS-01) und Teil der
    // Exporttabelle; in der echten Struktur steht es unterhalb der
    // "nicht im Flash"-Linie, wird aber sehr wohl mitgeschrieben.
    int    max_hop_text = 0;
};

extern s_meshcom_settings meshcom_settings;
