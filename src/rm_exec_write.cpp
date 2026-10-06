// rm_exec_write.cpp -- see rm_exec_ext.h. Write executors of the extended remote-management commands:
// name <text>, atxt <text>, pos <lat> <lon> <alt>. Runs on the loop task.
//
// The received args are validated with rm_text.h BEFORE anything is built from them. A text write goes
// through the console path (commandAction, the same handler the web GUI uses) and is read back: the
// handler trims, strips '#' and clips, so a mismatch means the console did not store what was asked.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "configuration.h"
#include "command_functions.h"
#include "gps_functions.h"
#include "loop_functions.h"
#include "loop_functions_extern.h"
#include "rm_exec_ext.h"
#include "rm_format.h"
#include "rm_text.h"

namespace
{

// The full reply literal ("err text", "err range", "err gps", "err failed") into res, returns -1.
int fail(char *res, size_t n, const char *reply)
{
    snprintf(res, n, "%s", reply);
    return -1;
}

// "ok <body>" where the body is written by the caller into res + 3 (a formatter that returns 0 when it
// does not fit). Returns 1, or the failure.
int okFrom(char *res, size_t n, size_t bodyLen)
{
    if (bodyLen == 0)
        return fail(res, n, "err failed");
    memcpy(res, "ok ", 3);
    return 1;
}

// name / atxt: validate, console line, read back.
int writeText(const RmCmd &c, char *res, size_t n, bool isName)
{
    const RmTextKind kind = isName ? RM_TEXT_NAME : RM_TEXT_ATXT;

    if (!rmTextAllowed(c.args, kind))
        return fail(res, n, "err text");

    // The --atxt handler maps the exact word "none" to an empty comment (the name check in rmTextAllowed
    // already refuses it case-insensitively). Refuse it here too: a literal comment "none" cannot be
    // stored, and the handler must not clear the field behind a read-back that then fails.
    if (!isName && strcmp(c.args, "none") == 0)
        return fail(res, n, "err text");

    // Longest line: "--setname " (10) + 19, "--atxt " (7) + 39. commandAction() keeps the case of the
    // argument (command_functions.cpp:711 copies the trimmed line verbatim; only the command name is matched
    // case-insensitively), so capitals survive.
    char line[64];
    snprintf(line, sizeof(line), isName ? "--setname %s" : "--atxt %s", c.args);
    commandAction(line, false);

    const char *stored = isName ? meshcom_settings.node_name : meshcom_settings.node_atxt;
    if (strcmp(stored, c.args) != 0)
        return fail(res, n, "err failed");

    char *body = res + 3;
    if (n < 4)
        return fail(res, n, "err failed");
    const size_t len = isName ? rmFmtName(body, n - 3, stored) : rmFmtAtxt(body, n - 3, stored);
    return okFrom(res, n, len);
}

// Stored magnitude + hemisphere letter back to a signed value.
double signedValue(double magnitude, char hemisphere)
{
    return (hemisphere == 'S' || hemisphere == 'W') ? -magnitude : magnitude;
}

// True when the stored double reproduces the requested value at 5 decimals.
bool sameAt5(double stored, double wanted)
{
    return fabs(stored - wanted) < 0.000005;
}

// pos: the three console setters (--setlat/--setlon/--setalt) each save; here the same five fields are
// set exactly as they do and settings are saved once.
int writePos(const RmCmd &c, char *res, size_t n)
{
    RmPosArg p;
    if (!rmPosParse(c.args, &p))
        return fail(res, n, "err range");

    // With the GPS chip on the next fix overwrites a manual position.
    if (bGPSON)
        return fail(res, n, "err gps");

    // Mirrors "--setlat" (command_functions.cpp:3998-4016): unsigned magnitude + hemisphere letter.
    meshcom_settings.node_lat_c = 'N';
    meshcom_settings.node_lat = p.lat;
    if (p.lat < 0)
    {
        meshcom_settings.node_lat_c = 'S';
        meshcom_settings.node_lat = fabs(p.lat);
    }

    // Mirrors "--setlon" (command_functions.cpp:4020-4038).
    meshcom_settings.node_lon_c = 'E';
    meshcom_settings.node_lon = p.lon;
    if (p.lon < 0)
    {
        meshcom_settings.node_lon_c = 'W';
        meshcom_settings.node_lon = fabs(p.lon);
    }

    // Mirrors "--setalt" (command_functions.cpp:4042-4075): the 0..40000 range is already enforced by
    // rmPosParse. The altitude filter / barometer reference follow the new value.
    meshcom_settings.node_alt = p.alt;
#ifdef ENABLE_GPS
    WZ_GPS_AltSeed((float)p.alt);
#else
    baroBaseRelatch((float)p.alt);
#endif

    save_settings(); // one flash write instead of three

    RmPosIn in;
    in.lat = signedValue(meshcom_settings.node_lat, meshcom_settings.node_lat_c);
    in.lon = signedValue(meshcom_settings.node_lon, meshcom_settings.node_lon_c);
    in.alt = meshcom_settings.node_alt;
    in.src = RM_POS_SET;

    if (!sameAt5(in.lat, p.lat) || !sameAt5(in.lon, p.lon) || in.alt != p.alt)
        return fail(res, n, "err failed");

    if (n < 4)
        return fail(res, n, "err failed");
    return okFrom(res, n, rmFmtPos(res + 3, n - 3, in));
}

} // namespace

int rmExecWrite(const RmCmd &c, char *res, size_t n)
{
    if (res == nullptr || n == 0 || c.args[0] == '\0')
        return 0; // empty args: the read executor answers

    if (strcmp(c.cmd, "name") == 0)
        return writeText(c, res, n, true);
    if (strcmp(c.cmd, "atxt") == 0)
        return writeText(c, res, n, false);
    if (strcmp(c.cmd, "pos") == 0)
        return writePos(c, res, n);
    return 0;
}
