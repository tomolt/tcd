#include "tcd.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static TcdType *cloneType(TcdType *in) {
	TcdType *out = malloc(sizeof(TcdType));
	memcpy(out, in, sizeof(TcdType));
	switch (in->tclass) {
		case TCDT_BASE: out->as.base.name = strdup(in->as.base.name); break;
		case TCDT_POINTER: out->as.pointer.to = cloneType(in->as.pointer.to); break;
		case TCDT_ARRAY:   out->as.array.of   = cloneType(in->as.array.of  ); break;
		default: break;
	}
	return out;
}

void cexprFreeType(TcdType *type) {
	switch (type->tclass) {
		case TCDT_BASE:
			free(type->as.base.name);
			break;
		case TCDT_POINTER:
			cexprFreeType(type->as.pointer.to);
			break;
		case TCDT_ARRAY:
			cexprFreeType(type->as.array.of);
			break;
		default: break;
	}
	free(type);
}

static int parseExpr(TcdContext*, const char**, TcdType**, TcdRtLoc*);

static int isSymbolBeg(char c) {
	return isalpha(c) || c == '_';
}

static int isSymbol(char c) {
	return isalnum(c) || c == '_';
}

static int isDigit(char c) {
	return isdigit(c);
}

static void skipSpace(const char **str) {
	while (**str == ' ' || **str == '\n') (*str)++;
}

static int parseSuffix(TcdContext *debug, const char **str, TcdType **type, TcdRtLoc *rtloc) {
	skipSpace(str);
	switch (**str) {
		case '[':
			(*str)++;
			TcdType *atype = *type;
			TcdRtLoc artloc = *rtloc;
			TcdType *itype;
			TcdRtLoc irtloc;
			if (parseExpr(debug, str, &itype, &irtloc) != 0) return -1;
			skipSpace(str);
			if (**str != ']') return -1;
			(*str)++;
			if (itype->tclass != TCDT_BASE) return -1;
			if (itype->as.base.interp != TCDI_SIGNED &&
				itype->as.base.interp != TCDI_UNSIGNED) return -1;
			int64_t index = 0; /* Sign bit */
			tcdReadRtLoc(debug, irtloc, itype->size, &index);
			if (tcdDerefIndex(debug, atype, artloc, index, type, rtloc) != 0) return -1;
			free(atype);
			return 0;
		default: return 1;
	}
}

static int parseSymbol(const char **str, char *symbol) {
	if (!isSymbolBeg(**str)) return -1;
	while (isSymbol(**str)) {
		*symbol = **str;
		(*str)++;
		symbol++;
	}
	*symbol = '\0';
	return 0;
}

static int parseNumber(const char **str, TcdType **type, TcdRtLoc *rtloc) {
	int64_t num = 0;
	if (!isDigit(**str)) return -1;
	while (isDigit(**str)) {
		num = num * 10 + *(*str)++ - '0';
	}
	if (**str == '.') {
		(*str)++;
		int32_t shift = 1;
		while (isDigit(**str)) {
			num = num * 10 + *(*str)++ - '0';
			shift *= 10;
		}
		double ret = (double)num / (double)shift;
		*type = malloc(sizeof(TcdType));
		(*type)->tclass = TCDT_BASE;
		(*type)->size = 8;
		(*type)->as.base.name = strdup("double");
		(*type)->as.base.interp = TCDI_FLOAT;
		rtloc->region = TCDR_HOST_TEMP;
		rtloc->address = *(uint64_t*)&ret;
	} else {
		*type = malloc(sizeof(TcdType));
		(*type)->tclass = TCDT_BASE;
		(*type)->size = 8;
		(*type)->as.base.name = strdup("long long");
		(*type)->as.base.interp = TCDI_SIGNED;
		rtloc->region = TCDR_HOST_TEMP;
		rtloc->address = *(uint64_t*)&num;
	}
	return 0;
}

static int parsePrimitive(TcdContext *debug, const char **str, TcdType **type, TcdRtLoc *rtloc) {
	skipSpace(str);
	if (**str == '(') {
		(*str)++;
		if (parseExpr(debug, str, type, rtloc) != 0) return -1;
		if (**str != ')') return -1;
		(*str)++;
	} else if (isSymbolBeg(**str)) {
		char symbol[128];
		if (parseSymbol(str, symbol) != 0) return -1;
		uint64_t rip = tcdReadIP(debug);
		TcdFunction *func = tcdSurroundingFunction(&debug->info, rip);
		if (func == NULL) return -1;
		for (int i = 0; i < func->numLocals; i++) {
			if (strcmp(func->locals[i].name, symbol) == 0) {
				*type = cloneType(func->locals[i].type);
				if (tcdInterpretLocation(debug,
					func->locals[i].locdesc, rtloc) != 0) return -1;
				goto PRIM_END;
			}
		}
		return -1;
	} else if (isDigit(**str)) {
		if (parseNumber(str, type, rtloc) != 0) return -1;
	} else {
		return -1;
	}
PRIM_END:
	switch (parseSuffix(debug, str, type, rtloc)) {
		case  0: goto PRIM_END;
		case -1: return -1;
		default: return  0;
	}
}

static int parsePrefix(TcdContext *debug, const char **str, TcdType **type, TcdRtLoc *rtloc) {
	TcdType *ptype;
	TcdRtLoc prtloc;
	skipSpace(str);
	switch (**str) {
		case '*':
			(*str)++;
			if (parsePrefix(debug, str, &ptype, &prtloc) != 0) return -1;
			if (ptype->tclass != TCDT_POINTER) return -1;
			TcdType *old = ptype;
			if (tcdDeref(debug, ptype, prtloc, type, rtloc) != 0) return -1;
			free(old);
			return 0;
		case '&':
			(*str)++;
			if (parsePrefix(debug, str, &ptype, &prtloc) != 0) return -1;
			if (prtloc.region != TCDR_ADDRESS) return -1;
			TcdType *new = malloc(sizeof(TcdType));
			new->tclass = TCDT_POINTER;
			new->size = 8;
			new->as.pointer.to = ptype;
			*type = new;
			rtloc->region = TCDR_HOST_TEMP; /* TODO */
			return 0;
		default:
			return parsePrimitive(debug, str, type, rtloc);
	}
}

#if 0
static int parseInfix(TcdContext *debug, const char **str, TcdType **type, TcdRtLoc *rtloc) {
	return -1;
}
#endif

static int parseExpr(TcdContext *debug, const char **str, TcdType **type, TcdRtLoc *rtloc) {
	return parsePrefix(debug, str, type, rtloc);
}

int cexprParse(TcdContext *debug, const char *str, TcdType **type, TcdRtLoc *rtloc) {
	const char *mstr = str;
	if (parseExpr(debug, &mstr, type, rtloc) != 0) return -1;
	return 0;
}
