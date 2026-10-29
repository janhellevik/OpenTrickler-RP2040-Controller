#ifndef PROFILE_H_
#define PROFILE_H_

#include <stdint.h>
#include <stdbool.h>
#include "http_rest.h"
#include "profile_store.h"


// Profile types, sizes and EEPROM_PROFILE_DATA_REV live in profile_store.h


#ifdef __cplusplus
extern "C" {
#endif



// Interface
bool profile_data_init(void);
bool profile_data_save();

profile_t * profile_select(uint8_t idx);
profile_t * profile_get_selected();
uint16_t profile_get_selected_idx();

// REST interface
bool http_rest_profile_config(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_profile_read(struct fs_file *file, int num_params, char *params[], char *values[]);
bool http_rest_profile_summary(struct fs_file *file, int num_params, char *params[], char *values[]);


#ifdef __cplusplus
}
#endif

#endif  // PROFILE_H_