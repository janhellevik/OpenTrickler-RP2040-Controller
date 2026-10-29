/*
 * Host test for the profile block migration in src/profile_store.c.
 *
 * Run with tests/run_host_tests.sh (needs only a C compiler).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile_store.h"


static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures += 1; \
    } \
} while (0)


// Same CRC-32 (polynomial 0xEDB88320) as the table in src/common.c
uint32_t software_crc32(void * data, size_t length) {
    const uint8_t * p = (const uint8_t *) data;
    uint32_t crc = 0xffffffff;

    while (length--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit += 1) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }

    return crc ^ 0xffffffff;
}


// What save_config writes: the block followed by its CRC32. The rest of the raw read is
// whatever the EEPROM held before (0xFF when erased).
static void make_raw(uint8_t * raw, const void * block, size_t size) {
    memset(raw, 0xFF, PROFILE_STORE_READ_SIZE);
    memcpy(raw, block, size);

    uint32_t crc = software_crc32((void *) block, size);
    memcpy(raw + size, &crc, sizeof(crc));
}


static void fill_v1(eeprom_profile_data_v1_t * v1) {
    memset(v1, 0x0, sizeof(*v1));
    v1->profile_data_rev = 0;
    v1->current_profile_idx = 5;

    for (int idx = 0; idx < MAX_PROFILE_CNT; idx += 1) {
        profile_v1_t * p = &v1->profiles[idx];
        p->rev = 100 + idx;
        p->compatibility = 200 + idx;
        snprintf(p->name, sizeof(p->name), "Powder %d,gr", idx);
        p->coarse_kp = 0.01f * (idx + 1);
        p->coarse_ki = 0.02f * (idx + 1);
        p->coarse_kd = 0.3f * (idx + 1);
        p->coarse_min_flow_speed_rps = 0.1f + idx;
        p->coarse_max_flow_speed_rps = 5.0f + idx;
        p->fine_kp = 2.0f + idx;
        p->fine_ki = 0.001f * idx;
        p->fine_kd = 10.0f + idx;
        p->fine_min_flow_speed_rps = 0.08f + idx;
        p->fine_max_flow_speed_rps = 3.0f + idx;
    }
}


static void check_migrated_from(const eeprom_profile_data_t * out, const eeprom_profile_data_v1_t * v1) {
    CHECK(out->profile_data_rev == 0);
    CHECK(out->current_profile_idx == v1->current_profile_idx);

    for (int idx = 0; idx < MAX_PROFILE_CNT; idx += 1) {
        const profile_t * p = &out->profiles[idx];
        const profile_v1_t * o = &v1->profiles[idx];

        CHECK(p->rev == o->rev);
        CHECK(p->compatibility == o->compatibility);
        CHECK(strcmp(p->name, o->name) == 0);
        CHECK(p->coarse_kp == o->coarse_kp);
        CHECK(p->coarse_ki == o->coarse_ki);
        CHECK(p->coarse_kd == o->coarse_kd);
        CHECK(p->coarse_min_flow_speed_rps == o->coarse_min_flow_speed_rps);
        CHECK(p->coarse_max_flow_speed_rps == o->coarse_max_flow_speed_rps);
        CHECK(p->fine_kp == o->fine_kp);
        CHECK(p->fine_ki == o->fine_ki);
        CHECK(p->fine_kd == o->fine_kd);
        CHECK(p->fine_min_flow_speed_rps == o->fine_min_flow_speed_rps);
        CHECK(p->fine_max_flow_speed_rps == o->fine_max_flow_speed_rps);
        CHECK(p->dopecard_id[0] == '\0');
        CHECK(p->dopecard_rev == 0);
    }
}


static eeprom_profile_data_t defaults;


static void test_sizes(void) {
    CHECK(sizeof(eeprom_profile_data_v1_t) == 516);
    CHECK(sizeof(eeprom_profile_data_t) == 772);
    CHECK(PROFILE_STORE_READ_SIZE <= 1024);

    char check[] = "123456789";
    CHECK(software_crc32(check, 9) == 0xCBF43926);
}


static void test_v1_block_is_migrated(void) {
    eeprom_profile_data_v1_t v1;
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    fill_v1(&v1);
    make_raw(raw, &v1, sizeof(v1));

    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_MIGRATED);
    check_migrated_from(&out, &v1);
}


static void test_v1_block_over_old_data_is_migrated(void) {
    eeprom_profile_data_v1_t v1;
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    fill_v1(&v1);
    make_raw(raw, &v1, sizeof(v1));
    // Bytes past the rev 1 CRC are not erased in practice
    for (size_t idx = sizeof(v1) + 4; idx < sizeof(raw); idx += 1) {
        raw[idx] = (uint8_t) (idx * 7);
    }

    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_MIGRATED);
    check_migrated_from(&out, &v1);
}


static void test_legacy_rev1_tag_is_migrated(void) {
    eeprom_profile_data_v1_t v1;
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    fill_v1(&v1);
    v1.profile_data_rev = EEPROM_PROFILE_DATA_V1_REV;
    memset(raw, 0xFF, sizeof(raw));
    memcpy(raw, &v1, sizeof(v1));   // Pre-CRC firmware: no checksum

    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_MIGRATED);
    v1.profile_data_rev = 0;
    check_migrated_from(&out, &v1);
}


static void test_migrated_block_round_trips(void) {
    eeprom_profile_data_v1_t v1;
    eeprom_profile_data_t migrated;
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    fill_v1(&v1);
    make_raw(raw, &v1, sizeof(v1));
    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &migrated) == PROFILE_STORE_MIGRATED);

    // Link a slot, then save it the way profile_data_save does, over the old block
    strcpy(migrated.profiles[3].dopecard_id, "65f1c0ffee0123456789abcd");
    migrated.profiles[3].dopecard_rev = 42;
    make_raw(raw, &migrated, sizeof(migrated));

    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_LOADED);
    CHECK(memcmp(&out, &migrated, sizeof(out)) == 0);
    CHECK(strcmp(out.profiles[3].dopecard_id, "65f1c0ffee0123456789abcd") == 0);
    CHECK(out.profiles[3].dopecard_rev == 42);
    CHECK(out.profiles[2].dopecard_id[0] == '\0');
}


static void test_erased_eeprom_gives_defaults(void) {
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    memset(raw, 0xFF, sizeof(raw));
    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_DEFAULTS);
    CHECK(memcmp(&out, &defaults, sizeof(out)) == 0);
}


static void test_corrupt_v1_block_gives_defaults(void) {
    eeprom_profile_data_v1_t v1;
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    fill_v1(&v1);
    make_raw(raw, &v1, sizeof(v1));
    raw[100] ^= 0x01;

    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_DEFAULTS);
}


static void test_short_read_gives_defaults(void) {
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE] = {0};

    CHECK(profile_store_decode(raw, 10, &defaults, &out) == PROFILE_STORE_DEFAULTS);
}


static void test_names_are_terminated_and_index_in_range(void) {
    eeprom_profile_data_v1_t v1;
    eeprom_profile_data_t out;
    uint8_t raw[PROFILE_STORE_READ_SIZE];

    fill_v1(&v1);
    memcpy(v1.profiles[1].name, "ABCDEFGHIJKLMNOP", PROFILE_NAME_MAX_LEN);    // 16 characters, no NUL
    v1.current_profile_idx = 9;
    make_raw(raw, &v1, sizeof(v1));

    CHECK(profile_store_decode(raw, sizeof(raw), &defaults, &out) == PROFILE_STORE_MIGRATED);
    CHECK(strcmp(out.profiles[1].name, "ABCDEFGHIJKLMNO") == 0);
    CHECK(out.current_profile_idx == 0);
}


int main(void) {
    memset(&defaults, 0x0, sizeof(defaults));
    strcpy(defaults.profiles[0].name, "Default0");
    defaults.profiles[0].fine_kp = 2.0f;

    test_sizes();
    test_v1_block_is_migrated();
    test_v1_block_over_old_data_is_migrated();
    test_legacy_rev1_tag_is_migrated();
    test_migrated_block_round_trips();
    test_erased_eeprom_gives_defaults();
    test_corrupt_v1_block_gives_defaults();
    test_short_read_gives_defaults();
    test_names_are_terminated_and_index_in_range();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }

    printf("profile_store_test: all checks passed\n");
    return 0;
}
