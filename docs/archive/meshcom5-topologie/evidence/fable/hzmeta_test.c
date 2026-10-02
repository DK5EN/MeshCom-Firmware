#include <stdint.h>
// naive HzMeta struct if not carefully split into homogeneous SoA arrays
struct HzMeta {
    uint16_t last_min;
    uint8_t hop_g;   // hop count + G-bit packed into one byte
};
_Static_assert(sizeof(struct HzMeta) == 3, "HzMeta must be 3 bytes");
int dummy;
