#include <string.h>

#include "profile_store.h"


// The profile block lives between 9K and 10K in EEPROM, followed by its CRC32
_Static_assert(sizeof(eeprom_profile_data_t) + sizeof(uint32_t) <= 1024, "Profile data exceeds its 1K EEPROM region");
_Static_assert(sizeof(eeprom_profile_data_v1_t) == 516, "Rev 1 profile layout must match what older firmware wrote");


static bool crc_matches(const uint8_t * raw, size_t size) {
    uint32_t stored_crc32;
    memcpy(&stored_crc32, raw + size, sizeof(stored_crc32));

    return software_crc32((void *) raw, size) == stored_crc32;
}


void profile_store_migrate_v1(const eeprom_profile_data_v1_t * in, eeprom_profile_data_t * out) {
    memset(out, 0x0, sizeof(eeprom_profile_data_t));

    out->profile_data_rev = in->profile_data_rev;
    out->current_profile_idx = in->current_profile_idx;

    for (int idx = 0; idx < MAX_PROFILE_CNT; idx += 1) {
        const profile_v1_t * src = &in->profiles[idx];
        profile_t * dst = &out->profiles[idx];

        dst->rev = src->rev;
        dst->compatibility = src->compatibility;
        memcpy(dst->name, src->name, sizeof(dst->name));

        dst->coarse_kp = src->coarse_kp;
        dst->coarse_ki = src->coarse_ki;
        dst->coarse_kd = src->coarse_kd;
        dst->coarse_min_flow_speed_rps = src->coarse_min_flow_speed_rps;
        dst->coarse_max_flow_speed_rps = src->coarse_max_flow_speed_rps;

        dst->fine_kp = src->fine_kp;
        dst->fine_ki = src->fine_ki;
        dst->fine_kd = src->fine_kd;
        dst->fine_min_flow_speed_rps = src->fine_min_flow_speed_rps;
        dst->fine_max_flow_speed_rps = src->fine_max_flow_speed_rps;

        // Unlinked: dopecard_id is "" and dopecard_rev is 0 from the memset
    }
}


profile_store_result_t profile_store_decode(const uint8_t * raw, size_t raw_len,
                                            const eeprom_profile_data_t * defaults,
                                            eeprom_profile_data_t * out) {
    profile_store_result_t result;
    uint16_t received_rev = 0xFFFF;

    if (raw_len >= PROFILE_STORE_READ_SIZE) {
        memcpy(&received_rev, raw, sizeof(received_rev));
    }

    // Current layout, CRC checked (what save_config writes)
    if (received_rev == 0 && crc_matches(raw, sizeof(eeprom_profile_data_t))) {
        memcpy(out, raw, sizeof(eeprom_profile_data_t));
        result = PROFILE_STORE_LOADED;
    }
    // Current layout tagged with its rev (the pre-CRC scheme in load_config)
    else if (received_rev == EEPROM_PROFILE_DATA_REV) {
        memcpy(out, raw, sizeof(eeprom_profile_data_t));
        result = PROFILE_STORE_MIGRATED;
    }
    // Rev 1 layout, CRC checked or tagged with rev 1
    else if ((received_rev == 0 && crc_matches(raw, sizeof(eeprom_profile_data_v1_t))) ||
             received_rev == EEPROM_PROFILE_DATA_V1_REV) {
        eeprom_profile_data_v1_t v1;
        memcpy(&v1, raw, sizeof(v1));
        profile_store_migrate_v1(&v1, out);
        result = PROFILE_STORE_MIGRATED;
    }
    else {
        memcpy(out, defaults, sizeof(eeprom_profile_data_t));
        result = PROFILE_STORE_DEFAULTS;
    }

    // The rev is only a pre-CRC marker; stored blocks carry 0 and are validated by CRC32
    out->profile_data_rev = 0;

    if (out->current_profile_idx >= MAX_PROFILE_CNT) {
        out->current_profile_idx = 0;
    }

    // Older firmware could store a 16 character name without its NUL
    for (int idx = 0; idx < MAX_PROFILE_CNT; idx += 1) {
        out->profiles[idx].name[PROFILE_NAME_MAX_LEN - 1] = '\0';
        out->profiles[idx].dopecard_id[PROFILE_DOPECARD_ID_LEN - 1] = '\0';
    }

    return result;
}
