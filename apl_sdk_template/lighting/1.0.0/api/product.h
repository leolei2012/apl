#ifndef PRODUCT_LIGHT_H
#define PRODUCT_LIGHT_H
#include <stdbool.h>
#include <stdint.h>

typedef void (*{{API_PREFIX}}_output)(bool on, uint8_t brightness, void *ctx);
typedef struct {
    bool power;
    uint8_t brightness;
    uint32_t elapsed_ms;
    {{API_PREFIX}}_output output;
    void *ctx;
} {{API_PREFIX}}_t;

/* Caller owns the instance and ctx. Calls are serial, outside interrupts. */
void {{API_PREFIX}}_init({{API_PREFIX}}_t *self, {{API_PREFIX}}_output output, void *ctx);
void {{API_PREFIX}}_tick({{API_PREFIX}}_t *self);
bool {{API_PREFIX}}_get_power(const {{API_PREFIX}}_t *self);
bool {{API_PREFIX}}_set_power({{API_PREFIX}}_t *self, bool value);
int32_t {{API_PREFIX}}_get_brightness(const {{API_PREFIX}}_t *self);
bool {{API_PREFIX}}_set_brightness({{API_PREFIX}}_t *self, int32_t value);
#endif
