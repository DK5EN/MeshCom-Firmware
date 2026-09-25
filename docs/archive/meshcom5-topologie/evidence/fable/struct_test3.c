#include <stdint.h>
struct EdgePoolRow {
    char call[10];
    int16_t lat16;
    int16_t lon16;
    uint16_t last_min;
    uint16_t rpt_min;
    uint8_t flags;
    uint8_t hw;
    uint64_t hears;
    uint64_t heardby;
};
_Static_assert(sizeof(struct EdgePoolRow) == 40, "size check 40");
int dummy;
