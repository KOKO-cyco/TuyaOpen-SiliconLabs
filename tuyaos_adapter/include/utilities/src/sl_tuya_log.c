#include "sl_tuya_log.h"

log_output_t sl_tuya_printf = printf;

void sl_tuya_log_output_set(log_output_t fn)
{
    if (fn != NULL) {
        sl_tuya_printf = fn;
    }
}

void log_printhex(char *ss, const uint8_t *buffs, int length)
{
    const uint8_t *d;
    int            r;

    SL_TUYA_PRINTF("%s \r\n", ss);
    for (int i = 0; i < length; i += 16) {
        d = &buffs[i];
        r = length - i;
        if (r > 16) {
            r = 16;
        }
        for (int j = 0; j < 16; j++) {
            if (j < r) {
                SL_TUYA_PRINTF("%02x ", d[j]);
            } else {
                SL_TUYA_PRINTF("   ");
            }
        }
        SL_TUYA_PRINTF("   ");
        for (int j = 0; j < r; j++) {
            if (d[j] < ' ' || d[j] > '~') {
                SL_TUYA_PRINTF(".");
            } else {
                SL_TUYA_PRINTF("%c", d[j]);
            }
        }
        SL_TUYA_PRINTF("%s", (char *)"\r\n");
    }
    SL_TUYA_PRINTF("%s", (char *)"\r\n");
}

void log_printhex_no_newline(char *ss, const uint8_t *buffs, int length)
{
    SL_TUYA_PRINTF("%s ", ss);
    for (int i = 0; i < length; i++) {
        SL_TUYA_PRINTF("%02x ", buffs[i]);
    }
    SL_TUYA_PRINTF("%s", (char *)"\r\n");
}
