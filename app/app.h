/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "STARFISH"
#define APP_SPLASH_RGB RGB(242, 108, 128)
#define APP_PALETTE 2u
#define APP_CANVAS_PX (240u * 120u)
#define APP_BLOCK 256u
#define APP_TOUCH
#define APP_BTDEV
#define APP_TICK
#define APP_SPLASH
#define APP_RESTART
#define APP_BLE_AT_BOOT (!starfish_ble_off())

int starfish_ble_off(void);

enum { OBJ_SETTINGS, OBJ_BANKX0, OBJ_BANKX1, OBJ_BANKX2, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xD4000u, 0xD6000u, 0xD8000u, 0xDC000u, 0xDE000u}

#define APP_BACKUP_MAGIC 0x53544632u
#define APP_BACKUP_VERSION 2u
#define APP_BACKUP_WHAT "replaces settings and Bank X"
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_BANKX0, OBJ_BANKX1, OBJ_BANKX2}

void app_init(void);
void app_splash(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_tick(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
