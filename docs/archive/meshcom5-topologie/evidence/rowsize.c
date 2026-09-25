#include <stdint.h>
struct R { char call[10]; int16_t lat16, lon16; uint16_t last_min, rpt_min; uint8_t flags, hw; uint64_t hears, heardby; };
struct E { uint8_t x, y, cnt; int8_t snr; uint16_t last_min; };
char r_size[sizeof(struct R)]; char r_align[_Alignof(uint64_t)]; char e_size[sizeof(struct E)];
