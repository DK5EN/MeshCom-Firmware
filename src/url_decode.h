#ifndef _URL_DECODE_H_
#define _URL_DECODE_H_

/**
 * Percent-decoding for WebUI query parameters (issue #1173).
 *
 * The browser sends every value through encodeURIComponent(), i.e. as
 * percent-encoded UTF-8. The previous decoder replaced a fixed list of
 * escapes (German umlauts, a few Italian vowels, ASCII punctuation) and left
 * every other one in the text verbatim, so a Polish "ą" went on-air as the
 * six characters "%C4%85". It also ran "%25" -> "%" before the later
 * replacements, so a typed "%28" arrived as "(".
 *
 * This decoder handles every escape in one left-to-right pass, so any UTF-8
 * character survives byte for byte and nothing is decoded twice.
 *
 * Rules:
 *   - '+'   becomes a space (form encoding).
 *   - %XX   (hex digits, either case) becomes the byte 0xXX.
 *   - a '%' not followed by two hex digits stays a literal '%'.
 *   - kept from the old decoder: %0D%0A becomes '-', %22 (") is dropped.
 *   - every other decoded C0 control byte (0x00-0x1F, incl. a lone CR or LF
 *     and TAB) and DEL are dropped, so a %00 cannot cut the C string short.
 *
 * Pure C++, no Arduino dependency -- unit-tested natively (test_url_decode).
 */

#include <stddef.h>
#include <stdint.h>

/** Decodes buf[0..len) in place and NUL-terminates it (buf needs len+1
 *  bytes). Returns the new length, which is never larger than len. */
size_t url_percent_decode(char *buf, size_t len);

/** Policies of urlDecode(). LEGACY is the rule set above (WebUI query values);
 *  STRICT is the Remote-page form decoder: '+' stays a literal '+', %22 is
 *  kept, a raw control byte or DEL or a decoded one (also %00) rejects the
 *  whole value. A TRUNC policy (cap on a UTF-8 boundary) is planned for
 *  webFieldDecode, not here. */
enum : uint8_t { URLD_LEGACY = 0, URLD_STRICT = 1 };

/** In place on buf[0..len), NUL at buf[ret] (buf needs len+1 bytes). Returns
 *  the new length (<= len); STRICT returns (size_t)-1 on a reject (the buffer
 *  content is then unspecified). Any policy value other than URLD_STRICT
 *  behaves as URLD_LEGACY. url_percent_decode(b, n) == urlDecode(b, n, URLD_LEGACY). */
size_t urlDecode(char *buf, size_t len, uint8_t policy);

#endif
