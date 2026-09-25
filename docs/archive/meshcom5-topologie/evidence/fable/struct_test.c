#include <stdint.h>
#include <stddef.h>

// Kantenpool-Papier row claim: call[10], six small fields, two uint64_t masks
struct EdgePoolRow {
    char call[10];
    uint8_t f1, f2, f3, f4, f5, f6;   // "sechs kleine Felder"
    uint64_t hears;
    uint64_t heardBy;
};

// This paper's proposed 12-byte core
struct NbrRowCore {
    int16_t lat16;
    int16_t lon16;
    uint16_t last_min;
    uint16_t rpt_min;
    uint8_t flags;
    uint8_t hw;
    uint8_t ncnt;
    uint8_t ext;
};

// Edge, claimed 6 bytes
struct NbrEdge {
    uint8_t x, y, cnt, snr;
    uint16_t last_min;
};

#include <stdio.h>
int main() {
    printf("EdgePoolRow: %zu\n", sizeof(struct EdgePoolRow));
    printf("NbrRowCore: %zu\n", sizeof(struct NbrRowCore));
    printf("NbrEdge: %zu\n", sizeof(struct NbrEdge));
    printf("alignof EdgePoolRow: %zu\n", _Alignof(struct EdgePoolRow));
    return 0;
}
