// include with definition of struct from esp_hid_host_main.c

#ifndef EHHM_H
#define EHHM_H

#include <sys/_stdint.h>

#define MAXBTDATALEN 4
struct sBtInput{
    uint16_t reportId;
    uint16_t len;
    uint8_t data[MAXBTDATALEN];
};
typedef struct sBtInput *sBtInput_t;
#endif
void app_main_hid_host(void);