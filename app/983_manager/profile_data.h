/*
 * profile_data.h - Generated profile tables for 983_manager
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

typedef enum
{
    PROFILE_DIP0_10G8 = 0u,
    PROFILE_DIP0_6G75,
    PROFILE_DIP1,
    PROFILE_DIP2,
    PROFILE_DIP3_OLDI,
    PROFILE_DIP4_10G8,
    PROFILE_DIP4_6G75,
    PROFILE_DIP5,
    PROFILE_DIP0_DIP1_10G8,
    PROFILE_DIP0_DIP1_6G75,
    PROFILE_DIP0_DIP2_10G8,
    PROFILE_DIP0_DIP2_6G75,
    PROFILE_COUNT
} profile_id_t;

typedef struct
{
    const char        *name;
    const init_op_t   *ops;
    uint16             op_count;
    const uint8       *edid;
    uint16             edid_len;
} profile_data_t;

extern const profile_data_t g_profile_data[PROFILE_COUNT];

#endif /* APP_983_MANAGER_PROFILE_DATA_H */
