#ifndef TCD_DATA_H
#define TCD_DATA_H

#include <stdint.h>

struct TcdLine {
	uint32_t number;
	uint64_t address;
};
typedef struct TcdLine TcdLine;

struct TcdLocal {
	char *name;
	uint8_t *exprloc;
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
	} base;
	struct {
		TcdTypeClass tclass;
		union TcdType *to;
	} pointer;
	struct {
		TcdTypeClass tclass;
		union TcdType *of;
	} array;
	struct {
		TcdTypeClass tclass;
		char *name;
	} struct_;
};
typedef union TcdType TcdType;

struct TcdBreakpoint {
	uint64_t address;
	TcdFunction *func;
	uint32_t line;
	uint64_t saved;
};
typedef struct TcdBreakpoint TcdBreakpoint;

struct TcdCompUnit {
	char *name;
	char *compDir;
	char *producer;
	uint64_t begin, end;
	TcdFunction *funcs;
	uint32_t numFuncs;
	/* TcdStruct *structs;
	uint32_t numStructs; */
};
typedef struct TcdCompUnit TcdCompUnit;

struct TcdInfo {
	TcdCompUnit *compUnits;
	uint32_t numCompUnits;
};
typedef struct TcdInfo TcdInfo;

struct TcdContext {
	int pid;
	int status;
	TcdInfo info;
	TcdBreakpoint *breaks;
	uint32_t numBreaks;
};
typedef struct TcdContext TcdContext;

int tcdLoadInfo(const char*, TcdInfo*);
TcdCompUnit *tcdSurroundingCompUnit(TcdInfo*, uint64_t);
TcdFunction *tcdSurroundingFunction(TcdInfo*, uint64_t);
TcdFunction *tcdFunctionByName(TcdInfo*, char*);
TcdLine *tcdNearestLine(TcdFunction*, uint64_t);
void tcdFreeInfo(TcdInfo*);
void tcdFreeContext(TcdContext*);

#endif
