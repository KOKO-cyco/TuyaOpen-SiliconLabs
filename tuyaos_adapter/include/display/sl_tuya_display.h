#ifndef SL_TUYA_DISPLAY
#define SL_TUYA_DISPLAY

#include <stdint.h>

void sl_tuya_display_init(void);

void sl_tuya_display_flush(uint16_t w, uint16_t h, uint8_t *data, uint16_t length);

#endif /* */
