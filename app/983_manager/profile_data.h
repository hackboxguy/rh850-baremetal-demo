/*
 * profile_data.h - generated from panels.json / bios-ver-11.txt
 *
 * Do not hand-edit this file. Re-run:
 *   python3 rh850-baremetal-demo/tools/gen_983_manager_profiles.py
 */

#ifndef APP_983_MANAGER_PROFILE_DATA_H
#define APP_983_MANAGER_PROFILE_DATA_H

#include "dr7f701686.dvf.h"

typedef enum
{
    INIT_OP_WRITE = 0u,
    INIT_OP_READ = 1u,
    INIT_OP_DELAY_MS = 2u,
    INIT_OP_LOAD_EDID = 3u
} init_op_type_t;

typedef struct
{
    uint8  type;
    uint8  dev_addr;
    uint8  reg_addr;
    uint8  value;
    uint16 delay_ms;
} init_op_t;

typedef struct
{
    uint8        type;
    const void  *data;
    uint16       count;
} init_block_t;

typedef enum
{
    INIT_BLOCK_OPS = 0u,
    INIT_BLOCK_PACKED_WRITES = 1u
} init_block_type_t;

typedef enum
{
    PROFILE_DIP0_10G8 = 0u,
    PROFILE_DIP0_6G75 = 1u,
    PROFILE_DIP1 = 2u,
    PROFILE_DIP2 = 3u,
    PROFILE_DIP3_OLDI = 4u,
    PROFILE_DIP4_10G8 = 5u,
    PROFILE_DIP4_6G75 = 6u,
    PROFILE_DIP5 = 7u,
    PROFILE_DIP0_DIP1_10G8 = 8u,
    PROFILE_DIP0_DIP1_6G75 = 9u,
    PROFILE_DIP0_DIP2_10G8 = 10u,
    PROFILE_DIP0_DIP2_6G75 = 11u,
    PROFILE_COUNT
} profile_id_t;

typedef struct
{
    const char        *name;
    const init_block_t *blocks;
    uint8              block_count;
    const uint8       *edid;
    uint16             edid_len;
} profile_data_t;

extern const profile_data_t g_profile_data[PROFILE_COUNT];
const profile_data_t *profile_select(uint8 dip_on_mask);

#endif /* APP_983_MANAGER_PROFILE_DATA_H */
