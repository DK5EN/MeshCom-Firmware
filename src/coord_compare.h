/**
 * Vergleich einer gespeicherten Koordinate (node_lat / node_lon, beide
 * double) mit dem Text, den das Web-Config-Feld liefert.
 *
 * Ursprung des Fehlers (PR #1150): node_lat/node_lon sind double, der nRF52-
 * Core hat aber kein String::toDouble() -- der #else-Zweig verglich deshalb
 * gegen paramValue.toFloat(). Ein float kann 49.997 nicht exakt darstellen
 * (er wird zu 49.9970016f), also scheiterte der Vergleich fuer praktisch
 * jede reale Koordinate und die GUI zeigte "Value could not be set.", obwohl
 * der Knoten den Wert laengst gespeichert hatte. Deshalb ist der Parameter
 * hier double, nicht float: atof() liefert auf beiden Cores double, und nur
 * damit bleibt 49.997 als 49.997 erhalten.
 *
 * Die Toleranz 1e-7 faengt das Rauschen der Text->double-Wandlung ab, nicht
 * einen fachlichen Unterschied -- die achte Nachkommastelle einer Koordinate
 * entspricht rund 1 mm, also weit unterhalb dessen, was GPS oder Tippen
 * ueberhaupt hergeben.
 *
 * Das innere fabs() ist noetig, weil --setlat/--setlon negative Eingaben als
 * Betrag ablegen; die Himmelsrichtung (S/W) steckt separat in node_lat_c
 * bzw. node_lon_c, nicht im Vorzeichen von node_lat/node_lon.
 *
 * Pure C++, keine Arduino-Abhaengigkeit -- nativ testbar (test_web_setcoord).
 */
#pragma once

#include <cmath>
#include <cstdlib>

inline bool coordMatches(double stored, const char *typed)
{
    return fabs(stored - fabs(atof(typed))) < 1e-7;
}
