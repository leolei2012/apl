/** Compile with only apl_wifi.c: this module must not depend on core or HMI. */
#include "apl_wifi_internal.h"
#include <assert.h>

int main(void)
{
    struct apl_wifi wifi;
    apl_wifi_reset(&wifi);
    assert(wifi.state == APL_WIFI_OFF && wifi.left_s == 0u);
    apl_wifi_pair(&wifi, 2u);
    apl_wifi_second(&wifi);
    assert(wifi.state == APL_WIFI_PAIRING && wifi.left_s == 1u);
    apl_wifi_second(&wifi);
    assert(wifi.state == APL_WIFI_OFF && wifi.left_s == 0u);
    apl_wifi_pair(&wifi, 100u);
    apl_wifi_connected(&wifi, 1u);
    apl_wifi_cancel_pairing(&wifi);
    apl_wifi_second(&wifi);
    assert(wifi.state == APL_WIFI_CONNECTED && wifi.left_s == 0u);
    apl_wifi_connected(&wifi, 0u);
    assert(wifi.state == APL_WIFI_OFF);
    apl_wifi_pair(&wifi, 100u);
    apl_wifi_cancel_pairing(&wifi);
    assert(wifi.state == APL_WIFI_OFF && wifi.left_s == 0u);
    return 0;
}
