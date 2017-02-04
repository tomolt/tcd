#ifndef TCD_H
#define TCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ----- Error Codes ----- */

enum TcdErrorCode {
	TCDE_OK,
	TCDE_LOAD_OPEN,
	TCDE_LOAD_INFO,
	TCDE_LOAD_COMP_UNIT,
	TCDE_LOAD_LINES,
	TCDE_LOAD_FUNCTION,
	TCDE_LOAD_LOCAL,
	TCDE_LOAD_TYPE
};
typedef enum TcdErrorCode TcdErrorCode;

const char *tcdFormulateErrorMessage(int code);

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

/* ----- Type ----- */

enum TcdTypeClass {
	TCDT_BASE, TCDT_POINTER, TCDT_ARRAY, TCDT_STRUCT
};
typedef enum TcdTypeClass TcdTypeClass;

enum TcdBaseInterp {
	TCDI_ADDRESS,
	TCDI_SIGNED,
	TCDI_UNSIGNED,
	TCDI_CHAR,
	TCDI_UCHAR,
	TCDI_FLOAT,
	TCDI_BOOL
};
typedef enum TcdBaseInterp TcdBaseInterp;

struct TcdType {
	TcdTypeClass tclass;
	uint32_t size;
	union {
		struct {
			char *name;
			TcdBaseInterp interp;
		} base;
		struct {
			struct TcdType *to;
		} pointer;
		struct {
			struct TcdType *of;
		} array;
		struct {
			char *name;
		} struc;
	} as;
};
typedef struct TcdType TcdType;

/* ----- Info ----- */

struct TcdLine {
	uint32_t number;
	uint64_t address;
};
typedef struct TcdLine TcdLine;

struct TcdLocal {
	char *name;
	TcdLocDesc locdesc;
	TcdType *type;
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

void tcdReadRtLoc(TcdContext*, TcdRtLoc, uint32_t, void*);

void tcdStepInstruction(TcdContext*);
uint64_t tcdStep(TcdContext*);
uint64_t tcdNext(TcdContext*);

uint16_t tcdGetStackTrace(TcdContext*, uint64_t*, int);

void tcdInsertBreakpoint(TcdContext*, uint64_t, uint32_t);

/* ----- Address Functions ----- */

int tcdInterpretLocation(TcdContext*, TcdLocDesc, TcdRtLoc*);

int tcdDeref(TcdContext*, TcdType*, TcdRtLoc, TcdType**, TcdRtLoc*);
int tcdDerefIndex(TcdContext*, TcdType*, TcdRtLoc, uint64_t, TcdType**, TcdRtLoc*);

void cexprFreeType(TcdType*);
int cexprParse(TcdContext*, const char*, TcdType**, TcdRtLoc*);

#ifdef __cplusplus
}
#endif

#endif
