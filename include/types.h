#ifndef TYPES_H
#define TYPES_H

#include "token.h"
#include "essentials/dynarr.h"
#include <stddef.h>
#include <stdint.h>

typedef uint8_t  byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef uint64_t qword;

typedef enum x64_register{
    RAX,
    RCX,
    RDX,
    RBX,
    RSP,
    RBP,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15
}X64Register;

typedef struct bstr{
    size_t len;
    const char *buff;
}BStr;

#endif
