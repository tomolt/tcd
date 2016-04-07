#ifndef TCD_ADDR_H
#define TCD_ADDR_H

#include <stdint.h>

#include "data.h"

struct TcdLocDesc {
	uint8_t *expr;
	uint64_t baseAddress;
};

struct TcdRtLoc {
	uint64_t address;
	enum {
		TCDR_ADDRESS,
		TCDR_REGISTER,
		TCDR_HOST_TEMP
	} region;
};

typedef struct TcdLocDesc TcdLocDesc;
typedef struct TcdRtLoc   TcdRtLoc;

int tcdInterpretLocation(TcdContext*, TcdLocDesc, TcdRtLoc*);

int tcdDeref     (TcdContext*, TcdType, TcdRtLoc,           TcdType*, TcdRtLoc*);
int tcdDerefIndex(TcdContext*, TcdType, TcdRtLoc, uint64_t, TcdType*, TcdRtLoc*);

#endif
