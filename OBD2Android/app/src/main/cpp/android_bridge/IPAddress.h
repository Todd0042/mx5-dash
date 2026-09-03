#pragma once
#include <stdint.h>
struct IPAddress {
    uint8_t bytes[4];
    IPAddress(uint8_t a=0, uint8_t b=0, uint8_t c=0, uint8_t d=0) {
        bytes[0]=a; bytes[1]=b; bytes[2]=c; bytes[3]=d;
    }
};
