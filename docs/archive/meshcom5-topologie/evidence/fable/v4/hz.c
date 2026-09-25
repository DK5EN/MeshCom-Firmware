#include <stdint.h>
struct HzMetaNaive { uint16_t last_min; uint8_t hop_g; };
_Static_assert(sizeof(struct HzMetaNaive) == 4, "naive struct pads to 4");
uint8_t hzMetaBytes[96][3];
_Static_assert(sizeof(hzMetaBytes) == 288, "byte array is 3 B per entry");
uint16_t hzLastMin[96]; uint8_t hzHopG[96];
_Static_assert(sizeof(hzLastMin) + sizeof(hzHopG) == 288, "SoA is 3 B per entry");
