/* queued dialogue pages produced by VM TEXT ops (event/result region CC00, design doc 11.4) */
#ifndef GB_DIALOGQ_H
#define GB_DIALOGQ_H
#include <stdint.h>
#define DQ_MAX 16u
typedef struct { uint8_t n; uint8_t speaker[DQ_MAX]; uint16_t text[DQ_MAX]; } DialogQ;
#include "eventram.h"
#define DQ ((DialogQ *)ER_DIALOGQ)
#endif
