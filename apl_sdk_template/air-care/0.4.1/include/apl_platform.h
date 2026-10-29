/**
 * @file apl_platform.h
 * @brief Platform types and calling convention. No MCU register dependency.
 *
 * Native C11 keeps its existing ABI. Keil C51 uses byte alignment and a LARGE
 * reentrant software stack; every library function and injected callback must
 * use APL_CALL consistently. This does not make concurrent access to one APL
 * instance safe. Configure and initialize the target stack before calling APL.
 */
#ifndef APL_PLATFORM_H
#define APL_PLATFORM_H

#include <stddef.h>

#ifdef APL_PLATFORM_TYPES_HEADER
#include APL_PLATFORM_TYPES_HEADER
#elif defined(__C51__)
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned int uint16_t;
typedef signed int int16_t;
typedef unsigned long uint32_t;
typedef signed long int32_t;
#else
#include <stdint.h>
#endif

#if defined(__C51__)
#ifndef __bool_true_false_are_defined
typedef unsigned char bool;
#define true 1u
#define false 0u
#define __bool_true_false_are_defined 1
#endif
#ifndef UINT8_MAX
#define UINT8_MAX 0xFFu
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 0xFFFFu
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFUL
#endif
#ifndef INT8_MAX
#define INT8_MAX 127
#define INT8_MIN (-127 - 1)
#endif
#ifndef INT16_MAX
#define INT16_MAX 32767
#define INT16_MIN (-32767 - 1)
#endif
#ifndef INT32_MAX
#define INT32_MAX 2147483647L
#define INT32_MIN (-2147483647L - 1L)
#endif
#define APL_CALL reentrant
#define APL_UNUSED_PARAM(value) ((value) = (value))
#define APL_ALIGN_INTEGER long
/** Repeated compatible extern declarations allocate no storage. */
#define APL_STATIC_ASSERT(condition, message) \
    extern char apl_static_assertion[(condition) ? 1 : -1]
/** C51 objects are byte aligned; verified below rather than assuming host ABI. */
#define APL_ALIGNOF(type) 1u
struct apl_platform_alignment_probe
{
    uint8_t prefix;
    uint32_t value;
};
APL_STATIC_ASSERT(sizeof(uint8_t) == 1u, "uint8 width");
APL_STATIC_ASSERT(sizeof(uint16_t) == 2u, "uint16 width");
APL_STATIC_ASSERT(sizeof(uint32_t) == 4u, "uint32 width");
APL_STATIC_ASSERT(offsetof(struct apl_platform_alignment_probe, value) == 1u,
                  "C51 byte alignment");
#else
#include <stdbool.h>
#define APL_CALL
#define APL_UNUSED_PARAM(value) ((void)(value))
#define APL_ALIGN_INTEGER long long
#ifdef __cplusplus
#define APL_STATIC_ASSERT(condition, message) static_assert(condition, message)
#define APL_ALIGNOF(type) alignof(type)
#else
#define APL_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#define APL_ALIGNOF(type) _Alignof(type)
#endif
#endif

#endif /** APL_PLATFORM_H */
