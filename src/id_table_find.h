// id_table_find.h -- lookups in the two msg_id tables (pure, header-only).
//
// The dedup ring ringBufferLoraRX[MAX_DEDUP_RING][5] and the own-message table own_msg_id[MAX_RING][5] share one row
// layout: bytes 0..3 msg_id little endian, byte 4 a flag (server flag / status). The linear searches over them live
// here once, used by dedup_functions.cpp, checkOwnTx() (loop_functions.cpp) and ack_rx_core.h, so a bounded proof
// of the ACK core checks the same loop the firmware runs. Every loop is bounded by the row count the caller passes
// (a compile-time table size).
#ifndef ID_TABLE_FIND_H
#define ID_TABLE_FIND_H

#include <stdint.h>

static inline uint32_t idTableRowId(const uint8_t *row) // reads row[0..3]
{
    return (uint32_t)row[0] | ((uint32_t)row[1] << 8) | ((uint32_t)row[2] << 16) | ((uint32_t)row[3] << 24);
}

// First row whose bytes 0..3 equal id[0..3], -1 when none.
static inline int idTableFind(const uint8_t (*tbl)[5], int rows, const uint8_t id[4])
{
    for (int i = 0; i < rows; i++)
        if (tbl[i][0] == id[0] && tbl[i][1] == id[1] && tbl[i][2] == id[2] && tbl[i][3] == id[3])
            return i;
    return -1;
}

// Same lookup by numeric id.
static inline int idTableFindU32(const uint8_t (*tbl)[5], int rows, uint32_t id)
{
    for (int i = 0; i < rows; i++)
        if (idTableRowId(tbl[i]) == id)
            return i;
    return -1;
}

#endif // ID_TABLE_FIND_H
