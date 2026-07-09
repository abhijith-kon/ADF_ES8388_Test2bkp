#ifndef APP_OTG_H
#define APP_OTG_H

void app_otg_start(void);
void app_otg_stop(void);
int app_otg_get_progress(void); // 0 = unconnected, 1 = connected/mounted

#endif
