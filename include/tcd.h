#ifndef TCD_H
#define TCD_H

#include <stdint.h>

/* ----- Address ----- */

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

/* ----- Info ----- */

struct TcdLine {
	uint32_t number;
	uint64_t address;
};
typedef struct TcdLine TcdLine;

struct TcdLocal {
	char *name;
	struct TcdLocDesc locdesc;
	uint64_t type;
};
typedef struct TcdLocal TcdLocal;

struct TcdFunction {
	char *name;
	uint64_t begin, end;
	TcdLine *lines;
	uint32_t numLines;
	TcdLocal *locals;
	uint32_t numLocals;
};
typedef struct TcdFunction TcdFunction;

enum TcdTypeClass {
	TCDT_BASE, TCDT_POINTER, TCDT_ARRAY, TCDT_STRUCT
};
typedef enum TcdTypeClass TcdTypeClass;

enum {
	TCDI_ADDRESS,
	TCDI_SIGNED,
	TCDI_UNSIGNED,
	TCDI_CHAR,
	TCDI_UCHAR,
	TCDI_FLOAT,
	TCDI_BOOL
};

union TcdType {
	TcdTypeClass tclass;
	struct {
		TcdTypeClass tclass;
		char *name;
		uint8_t size;
		uint8_t inter;
		uint64_t off;
	} base;
	struct {
		TcdTypeClass tclass;
		uint64_t to;
	} pointer;
	struct {
		TcdTypeClass tclass;
		uint64_t of;
	} array;
	struct {
		TcdTypeClass tclass;
		char *name;
	} struc;
};
typedef union TcdType TcdType;

struct TcdCompUnit {
	char *name;
	char *compDir;
	char *producer;
	uint64_t begin, end;
	TcdFunction *funcs;
	uint32_t numFuncs;
	TcdType *types;
	uint32_t numTypes;
	/* TcdStruct *structs;
	uint32_t numStructs; */
};
typedef struct TcdCompUnit TcdCompUnit;

struct TcdInfo {
	TcdCompUnit *compUnits;
	uint32_t numCompUnits;
};
typedef struct TcdInfo TcdInfo;

int tcdLoadInfo(const char*, TcdInfo*);
TcdCompUnit *tcdSurroundingCompUnit(TcdInfo*, uint64_t);
TcdFunction *tcdSurroundingFunction(TcdInfo*, uint64_t);
TcdFunction *tcdFunctionByName(TcdInfo*, char*);
TcdLine *tcdNearestLine(TcdFunction*, uint64_t);
void tcdFreeInfo(TcdInfo*);

/* ----- Context ----- */

struct TcdBreakpoint {
	uint64_t address;
	TcdFunction *func;
	uint32_t line;
	uint64_t saved;
};
typedef struct TcdBreakpoint TcdBreakpoint;

struct TcdContext {
	int pid;
	int status;
	TcdInfo info;
	TcdBreakpoint *breaks;
	uint32_t numBreaks;
};
typedef struct TcdContext TcdContext;

void tcdFreeContext(TcdContext*);

/* ----- Control ----- */

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

/* ----- Address Functions ----- */

int tcdInterpretLocation(TcdContext*, TcdLocDesc, TcdRtLoc*);

int tcdDeref     (TcdContext*, TcdType, TcdRtLoc,           TcdType*, TcdRtLoc*);
int tcdDerefIndex(TcdContext*, TcdType, TcdRtLoc, uint64_t, TcdType*, TcdRtLoc*);

#endif
