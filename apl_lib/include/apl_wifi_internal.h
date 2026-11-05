/** Private Wi-Fi state machine. No dependency on core, HMI, sensors or loads. */
#ifndef APL_WIFI_INTERNAL_H
#define APL_WIFI_INTERNAL_H
#include "apl_platform.h"
#include "apl_build_config.h"
#include "apl_type.h"
#if APL_COMPILE_WIFI
struct apl_wifi
{
    uint32_t left_s;
    enum apl_wifi_state state;
};
void apl_wifi_pair(struct apl_wifi *self, uint32_t timeout_s) APL_CALL;
void apl_wifi_connected(struct apl_wifi *self, uint8_t connected) APL_CALL;
void apl_wifi_second(struct apl_wifi *self) APL_CALL;
void apl_wifi_cancel_pairing(struct apl_wifi *self) APL_CALL;
void apl_wifi_reset(struct apl_wifi *self) APL_CALL;
#endif
#endif
