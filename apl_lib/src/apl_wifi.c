/** Optional Wi-Fi state machine. With APL_COMPILE_WIFI=0 this unit is empty. */
#include "apl_wifi_internal.h"
#if APL_COMPILE_WIFI
void apl_wifi_pair(struct apl_wifi *self, uint32_t timeout_s) APL_CALL
{
    self->state = APL_WIFI_PAIRING;
    self->left_s = timeout_s;
}
void apl_wifi_connected(struct apl_wifi *self, uint8_t connected) APL_CALL
{
    self->state = connected ? APL_WIFI_CONNECTED : APL_WIFI_OFF;
    self->left_s = 0u;
}
void apl_wifi_second(struct apl_wifi *self) APL_CALL
{
    if (0u != self->left_s)
    {
        self->left_s--;
        if ((0u == self->left_s) && (APL_WIFI_PAIRING == self->state))
        { self->state = APL_WIFI_OFF; }
    }
}
void apl_wifi_cancel_pairing(struct apl_wifi *self) APL_CALL
{
    if (APL_WIFI_PAIRING == self->state) { apl_wifi_reset(self); }
}
void apl_wifi_reset(struct apl_wifi *self) APL_CALL
{
    self->state = APL_WIFI_OFF;
    self->left_s = 0u;
}
#endif
