#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// Exact fields per Kantenpool-Papier: call[10], lat16, lon16, last_min, rpt_min, flags, hw, hears(uint64), heardby(uint64)
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

int main() {
    printf("EdgePoolRow: %zu\n", sizeof(struct EdgePoolRow));
    printf("offsetof lat16=%zu lon16=%zu last_min=%zu rpt_min=%zu flags=%zu hw=%zu hears=%zu heardby=%zu\n",
        offsetof(struct EdgePoolRow, lat16), offsetof(struct EdgePoolRow, lon16),
        offsetof(struct EdgePoolRow, last_min), offsetof(struct EdgePoolRow, rpt_min),
        offsetof(struct EdgePoolRow, flags), offsetof(struct EdgePoolRow, hw),
        offsetof(struct EdgePoolRow, hears), offsetof(struct EdgePoolRow, heardby));
    return 0;
}
