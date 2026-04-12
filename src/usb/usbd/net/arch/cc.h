#ifndef __ARCH_CC_H__
#define __ARCH_CC_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;
typedef int       sys_prot_t;

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#define BYTE_ORDER LITTLE_ENDIAN

#define LWIP_PLATFORM_DIAG(x)  do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP ASSERT: %s\n", x); while(1); } while(0)
#define LWIP_RAND() ((u32_t)rand())

#endif
