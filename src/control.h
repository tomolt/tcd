#ifndef TCD_CONTROL_H
#define TCD_CONTROL_H

#include "data.h"

void tcdSync(TcdContext*);

void tcdReadMemory (TcdContext*, uint64_t, uint32_t, void*);
void tcdWriteMemory(TcdContext*, uint64_t, uint32_t, void*);

uint64_t tcdReadIP(TcdContext*);
uint64_t tcdReadBP(TcdContext*);

void tcdStepInstruction(TcdContext*);
uint64_t tcdStep(TcdContext*);
uint64_t tcdNext(TcdContext*);

/* TODO move into separate / own module? */
uint16_t tcdGetStackTrace(TcdContext*, uint64_t*, uint16_t);

#endif
