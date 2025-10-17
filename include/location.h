#ifndef LOCATION_H
#define LOCATION_H

#include "types.h"
#include "token.h"

typedef enum location_type{
    LITERAL_LOCATION_TYPE,
    REGISTER_LOCATION_TYPE,
    LABEL_LOCATION_TYPE,
}LocationType;

typedef struct literal_location{
    dword value;
}LiteralLocation;

typedef struct register_location{
    X64Register reg;
}RegisterLocation;

typedef struct label_location{
    Token *label_token;
}LabelLocation;

typedef struct location{
    LocationType type;
    void *sub_location;
}Location;

#endif