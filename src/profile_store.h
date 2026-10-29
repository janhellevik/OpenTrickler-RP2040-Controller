#ifndef PROFILE_STORE_H_
#define PROFILE_STORE_H_

/*
 * EEPROM layout of the profile block and its migration between revisions.
 *
 * This header and profile_store.c only use the C standard library, so the
 * migration can be compiled and tested on the host (see tests/profile_store_test.c).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#define PROFILE_NAME_MAX_LEN    16
#define MAX_PROFILE_CNT         8
#define PROFILE_DOPECARD_ID_LEN 25          // 24 characters plus NUL

#define EEPROM_PROFILE_DATA_REV             2           // 16 bit
#define EEPROM_PROFILE_DATA_V1_REV          1           // Layout before the Dopecard link

typedef struct
{
    uint32_t rev;
    uint32_t compatibility;

    char name[PROFILE_NAME_MAX_LEN];

    float coarse_kp;
    float coarse_ki;
    float coarse_kd;

    float coarse_min_flow_speed_rps;
    float coarse_max_flow_speed_rps;

    float fine_kp;
    float fine_ki;
    float fine_kd;

    float fine_min_flow_speed_rps;
    float fine_max_flow_speed_rps;

    // Link to the Dopecard profile this slot was loaded from ("" and 0 when unlinked)
    char dopecard_id[PROFILE_DOPECARD_ID_LEN];
    uint32_t dopecard_rev;
} profile_t;


typedef struct {
    uint16_t profile_data_rev;
    uint16_t current_profile_idx;

    profile_t profiles[MAX_PROFILE_CNT];
} eeprom_profile_data_t;


// Rev 1 layout, kept to migrate EEPROM written by older firmware
typedef struct
{
    uint32_t rev;
    uint32_t compatibility;

    char name[PROFILE_NAME_MAX_LEN];

    float coarse_kp;
    float coarse_ki;
    float coarse_kd;

    float coarse_min_flow_speed_rps;
    float coarse_max_flow_speed_rps;

    float fine_kp;
    float fine_ki;
    float fine_kd;

    float fine_min_flow_speed_rps;
    float fine_max_flow_speed_rps;
} profile_v1_t;


typedef struct {
    uint16_t profile_data_rev;
    uint16_t current_profile_idx;

    profile_v1_t profiles[MAX_PROFILE_CNT];
} eeprom_profile_data_v1_t;


typedef enum {
    PROFILE_STORE_LOADED = 0,       // Valid block in the current layout
    PROFILE_STORE_MIGRATED,         // Converted from an older block, should be saved back
    PROFILE_STORE_DEFAULTS,         // Nothing valid was stored, defaults applied, should be saved back
} profile_store_result_t;

// Bytes to read from EEPROM for profile_store_decode: the current block plus its CRC32
#define PROFILE_STORE_READ_SIZE     (sizeof(eeprom_profile_data_t) + sizeof(uint32_t))


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode the raw profile block read from EEPROM into the current layout.
 *
 * Accepts, in order: a CRC-checked block in the current layout, a block tagged with
 * EEPROM_PROFILE_DATA_REV (the pre-CRC scheme load_config also accepts), and a rev 1
 * block in the old layout (CRC-checked, or tagged with EEPROM_PROFILE_DATA_V1_REV),
 * which is migrated with empty Dopecard links. Anything else yields the defaults.
 * Names and links are always NUL-terminated and the selected index is kept in range.
 *
 * @param raw PROFILE_STORE_READ_SIZE bytes read from EEPROM_PROFILE_DATA_BASE_ADDR
 */
profile_store_result_t profile_store_decode(const uint8_t * raw, size_t raw_len,
                                            const eeprom_profile_data_t * defaults,
                                            eeprom_profile_data_t * out);

/**
 * @brief Convert a rev 1 block to the current layout with empty Dopecard links.
 */
void profile_store_migrate_v1(const eeprom_profile_data_v1_t * in, eeprom_profile_data_t * out);

// Defined in common.c (the host test supplies its own)
uint32_t software_crc32(void * data, size_t length);

#ifdef __cplusplus
}
#endif

#endif  // PROFILE_STORE_H_
