/* buffer.h */
#include <stdint.h>

typedef struct buffer_item {
    uint8_t data[30];
    uint16_t cksum;
} BUFFER_ITEM;

#define BUFFER SIZE 10
