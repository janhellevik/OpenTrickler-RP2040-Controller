#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "profile.h"
#include "eeprom.h"
#include "common.h"


eeprom_profile_data_t profile_data;

extern void swuart_calcCRC(uint8_t* datagram, uint8_t datagramLength);

const eeprom_profile_data_t default_profile_data = {
    .profile_data_rev = 0,
    .profiles[0] = {
        .compatibility = 0,
        .name = "AR2208,gr",

        .coarse_kp = 0.025f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 5.0f,

        .fine_kp = 2.0f,
        .fine_ki = 0.0f,
        .fine_kd = 10.0f,
        .fine_min_flow_speed_rps = 0.08f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[1] = {
        .compatibility = 0,
        .name = "AR2209,gr",

        .coarse_kp = 0.05f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 8.0f,

        .fine_kp = 0.8f,
        .fine_ki = 0.0f,
        .fine_kd = 15.0f,
        .fine_min_flow_speed_rps = 0.08f,
        .fine_max_flow_speed_rps = 2.0f,
    },
    .profiles[2] = {
        .compatibility = 0,
        .name = "8208XBR,gr",

        .coarse_kp = 0.05f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 5.0f,

        .fine_kp = 2.0f,
        .fine_ki = 0.0f,
        .fine_kd = 12.0f,
        .fine_min_flow_speed_rps = 0.06f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[3] = {
        .compatibility = 0,
        .name = "Benchmark2,gr",

        .coarse_kp = 0.06f,
        .coarse_ki = 0.0f,
        .coarse_kd = 0.3f,
        .coarse_min_flow_speed_rps = 0.1f,
        .coarse_max_flow_speed_rps = 5.0f,

        .fine_kp = 0.8f,
        .fine_ki = 0.0f,
        .fine_kd = 15.0f,
        .fine_min_flow_speed_rps = 0.08f,
        .fine_max_flow_speed_rps = 5.0f,
    },
    .profiles[4] = {
        .compatibility = 0,
        .name = "Profile4",
    },
    .profiles[5] = {
        .compatibility = 0,
        .name = "Profile5",
    },
    .profiles[6] = {
        .compatibility = 0,
        .name = "Profile6",
    },
    .profiles[7] = {
        .compatibility = 0,
        .name = "Profile7",
    },
};


bool profile_data_save() {
    bool is_ok = save_config(EEPROM_PROFILE_DATA_BASE_ADDR, &profile_data, sizeof(profile_data));
    return is_ok;
}


bool profile_data_init() {
    bool is_ok = true;

    // Read the raw block rather than using load_config: load_config would replace a rev 1
    // block (the layout before the Dopecard link) with defaults and wipe the profiles.
    uint8_t * raw = malloc(PROFILE_STORE_READ_SIZE);
    if (!raw) {
        printf("Unable to allocate buffer with size: %d\n", (int) PROFILE_STORE_READ_SIZE);
        return false;
    }

    is_ok = eeprom_read(EEPROM_PROFILE_DATA_BASE_ADDR, raw, PROFILE_STORE_READ_SIZE);
    if (!is_ok) {
        printf("Unable to read profile data\n");
        free(raw);
        return false;
    }

    profile_store_result_t result = profile_store_decode(raw, PROFILE_STORE_READ_SIZE, &default_profile_data, &profile_data);
    free(raw);

    switch (result) {
        case PROFILE_STORE_LOADED:
            printf("Profile data read successfully\n");
            break;
        case PROFILE_STORE_MIGRATED:
            printf("Profile data migrated to rev %d\n", EEPROM_PROFILE_DATA_REV);
            is_ok = profile_data_save();
            break;
        case PROFILE_STORE_DEFAULTS:
        default:
            printf("No valid profile data, will populate with default profiles\n");
            is_ok = profile_data_save();
            break;
    }

    if (!is_ok) {
        printf("Unable to save profile data\n");
        return false;
    }

    // Register to eeprom save all
    eeprom_register_handler(profile_data_save);

    return true;
}


uint16_t profile_get_selected_idx() {
    return profile_data.current_profile_idx;
}


profile_t * profile_get_selected() {
    return &profile_data.profiles[profile_get_selected_idx()];
}


profile_t * profile_select(uint8_t idx) {
    profile_data.current_profile_idx = idx;

    return profile_get_selected(idx);
}


void profile_update_checksum() {
    swuart_calcCRC((uint8_t *) profile_get_selected(), sizeof(profile_t));
}

// Copy a fixed size string field into dst as the body of a JSON string: quotes and
// backslashes are escaped, control characters dropped. dst needs 2 * src_len bytes.
static void json_escape_field(char * dst, size_t dst_len, const char * src, size_t src_len) {
    size_t out = 0;

    for (size_t idx = 0; idx < src_len && src[idx] != '\0' && out + 2 < dst_len; idx += 1) {
        char c = src[idx];

        if (c == '"' || c == '\\') {
            dst[out++] = '\\';
            dst[out++] = c;
        }
        else if ((unsigned char) c >= 0x20) {
            dst[out++] = c;
        }
    }

    dst[out] = '\0';
}


static void profile_to_json(char * buf, size_t len, uint8_t profile_idx, const profile_t * profile) {
    char name[PROFILE_NAME_MAX_LEN * 2];
    char dopecard_id[PROFILE_DOPECARD_ID_LEN * 2];

    json_escape_field(name, sizeof(name), profile->name, sizeof(profile->name));
    json_escape_field(dopecard_id, sizeof(dopecard_id), profile->dopecard_id, sizeof(profile->dopecard_id));

    snprintf(buf, len, 
             "%s"
             "{\"pf\":%d,\"p0\":%ld,\"p1\":%ld,\"p2\":\"%s\",\"p3\":%0.3f,\"p4\":%0.3f,\"p5\":%0.3f,\"p6\":%0.3f,\"p7\":%0.3f,\"p8\":%0.3f,\"p9\":%0.3f,\"p10\":%0.3f,\"p11\":%0.3f,\"p12\":%0.3f,\"d0\":\"%s\",\"d1\":%lu}",
             http_json_header,
             profile_idx, 
             profile->rev,
             profile->compatibility,
             name,
             profile->coarse_kp,
             profile->coarse_ki,
             profile->coarse_kd,
             profile->coarse_min_flow_speed_rps,
             profile->coarse_max_flow_speed_rps,
             profile->fine_kp,
             profile->fine_ki,
             profile->fine_kd,
             profile->fine_min_flow_speed_rps,
             profile->fine_max_flow_speed_rps,
             dopecard_id,
             profile->dopecard_rev);
}


static void profile_invalid_index_to_json(char * buf, size_t len) {
    snprintf(buf, len, "%s{\"error\":\"InvalidProfileIndex\"}", http_json_header);
}


bool http_rest_profile_config(struct fs_file *file, int num_params, char *params[], char *values[]) {
    // Mappings:
    // pf (int): profile index
    // p0 (int): rev
    // p1 (int): compatibility
    // p2 (str): name (up to 15 characters)
    // p3 (float): coarse_kp
    // p4 (float): coarse_ki
    // p5 (float): coarse_kd
    // p6 (float): coarse_min_flow_speed_rps
    // p7 (float): coarse_max_flow_speed_rps
    // p8 (float): fine_kp
    // p9 (float): fine_ki
    // p10 (float): fine_kd
    // p11 (float): fine_min_flow_speed_rps
    // p12 (float): fine_max_flow_speed_rps
    // d0 (str): Dopecard profile id (up to 24 characters, "" when unlinked)
    // d1 (int): Dopecard profile revision (0 when unlinked)
    // ee (bool): save to eeprom
    static char buf[384];

    // Read the current loaded profile index
    uint8_t profile_idx = profile_get_selected_idx();

    // Overwrite the profile index (if applicable)
    // Clients rely on the index being read from the first parameter, so pf has to go first
    for (int idx = 0; idx < num_params; idx += 1) {
        if (strcmp(params[idx], "pf") == 0 && values[0] != NULL) {
            profile_idx = (uint16_t) atoi(values[0]);
        }
    }

    if (profile_idx >= MAX_PROFILE_CNT) {
        profile_invalid_index_to_json(buf, sizeof(buf));
    }

    else {
        profile_t * current_profile = profile_select(profile_idx);
        bool save_to_eeprom = false;

        // Control
        for (int idx = 0; idx < num_params; idx += 1) {
            // A parameter without a value carries nothing to set
            if (values[idx] == NULL) {
                continue;
            }

            if (strcmp(params[idx], "p0") == 0) {
                current_profile->rev = strtol(values[idx], NULL, 10);
            }
            else if (strcmp(params[idx], "p1") == 0) {
                current_profile->compatibility = strtol(values[idx], NULL, 10);
            }
            else if (strcmp(params[idx], "p2") == 0) {
                strncpy(current_profile->name, values[idx], sizeof(current_profile->name) - 1);
                current_profile->name[sizeof(current_profile->name) - 1] = '\0';
            }
            else if (strcmp(params[idx], "p3") == 0) {
                current_profile->coarse_kp = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p4") == 0) {
                current_profile->coarse_ki = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p5") == 0) {
                current_profile->coarse_kd = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p6") == 0) {
                current_profile->coarse_min_flow_speed_rps = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p7") == 0) {
                current_profile->coarse_max_flow_speed_rps = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p8") == 0) {
                current_profile->fine_kp = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p9") == 0) {
                current_profile->fine_ki = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p10") == 0) {
                current_profile->fine_kd = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p11") == 0) {
                current_profile->fine_min_flow_speed_rps = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "p12") == 0) {
                current_profile->fine_max_flow_speed_rps = strtof(values[idx], NULL);
            }
            else if (strcmp(params[idx], "d0") == 0) {
                strncpy(current_profile->dopecard_id, values[idx], sizeof(current_profile->dopecard_id) - 1);
                current_profile->dopecard_id[sizeof(current_profile->dopecard_id) - 1] = '\0';
            }
            else if (strcmp(params[idx], "d1") == 0) {
                current_profile->dopecard_rev = strtoul(values[idx], NULL, 10);
            }
            else if (strcmp(params[idx], "ee") == 0) {
                save_to_eeprom = string_to_boolean(values[idx]);
            }
        }

        // Perform action
        if (save_to_eeprom) {
            profile_data_save();
        }

        // Response
        profile_to_json(buf, sizeof(buf), profile_idx, current_profile);
    }

    size_t response_len = strlen(buf);
    file->data = buf;
    file->len = response_len;
    file->index = response_len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}


bool http_rest_profile_read(struct fs_file *file, int num_params, char *params[], char *values[]) {
    // Read only: never changes the selected profile
    // Mappings:
    // pf (int): profile index (defaults to the selected profile)
    // Response: the same fields as profile_config, including d0 and d1
    static char buf[384];

    long profile_idx = profile_get_selected_idx();

    for (int idx = 0; idx < num_params; idx += 1) {
        if (strcmp(params[idx], "pf") == 0) {
            char * end = NULL;
            profile_idx = values[idx] ? strtol(values[idx], &end, 10) : -1;

            // Reject an empty or non-numeric index rather than reading slot 0
            if (values[idx] == NULL || end == values[idx] || *end != '\0') {
                profile_idx = -1;
            }
        }
    }

    if (profile_idx < 0 || profile_idx >= MAX_PROFILE_CNT) {
        profile_invalid_index_to_json(buf, sizeof(buf));
    }
    else {
        profile_to_json(buf, sizeof(buf), (uint8_t) profile_idx, &profile_data.profiles[profile_idx]);
    }

    size_t response_len = strlen(buf);
    file->data = buf;
    file->len = response_len;
    file->index = response_len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}


bool http_rest_profile_summary(struct fs_file *file, int num_params, char *params[], char *values[])
{
    // It does not take argument
    assert(MAX_PROFILE_CNT <= 8);  // Ensures 256 byte buffer us sufficient
    static char buf[256];

    // Response
    // s0 (dict): A dictionary of all profiles in {idx: name} format. 
    // s1 (int): The current loaded profile index
    memset(buf, 0x0, sizeof(buf));
    const char * item_template = "\"%d\":\"%s\",";

    // Create header
    snprintf(buf, sizeof(buf), 
             "%s{\"s0\":{",
             http_json_header);

    size_t char_idx = strlen(buf);

    // Write profile information
    for (uint8_t p_idx=0; p_idx < MAX_PROFILE_CNT; p_idx+=1) {
        snprintf(&buf[char_idx], sizeof(buf) - char_idx, 
                 item_template,
                 p_idx, &profile_data.profiles[p_idx].name);
        char_idx += strnlen((const char *) &buf[char_idx], sizeof(buf));
    }

    // Append close bracket (replace the last comma)
    buf[char_idx - 1] = '}';

    // Append s1
    snprintf(&buf[char_idx], sizeof(buf) - char_idx,
             ",\"s1\":%d}", 
             profile_data.current_profile_idx);

    size_t response_len = strlen(buf);
    file->data = buf;
    file->len = response_len;
    file->index = response_len;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

    return true;
}