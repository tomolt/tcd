#include "tcd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>

/*****
 * USEFUL EXCERPT FROM THE LIBDWARF SOURCE CODE (dwarf_alloc.c, line 423)
 * ======================================================================
 *
 * dwarf_formstring(), for example, returns strings
 * which point into .debug_info or .debug_types but
 * dwarf_dealloc is never supposed to be applied
 * to strings dwarf_formstring() returns!
 * Lots of calls returning strings
 * have always been documented as requiring
 * dwarf_dealloc(...DW_DLA_STRING) when the code
 * just returns a pointer to a portion of a loaded section!
 * It is too late to change the documentation.
 *****/

#define CHECK_DWARF_RESULT(res) if (res == DW_DLV_ERROR) return ErrorCode
#define CHECK_LOAD_RESULT(res) if (res != TCDE_OK) return res

#define HANDLE_SUB_DIES(parent, cases) { \
	Dwarf_Die child; \
	res = dwarf_child(parent, &child, &error); \
	CHECK_DWARF_RESULT(res); \
	Dwarf_Die cur_die = child; \
	for (;;) { \
		Dwarf_Half tag; \
		res = dwarf_tag(cur_die, &tag, &error); \
		CHECK_DWARF_RESULT(res); \
		switch (tag) { \
			cases \
			default: break; \
		} \
		Dwarf_Die sib_die = 0; \
		res = dwarf_siblingof(dbg, cur_die, &sib_die, &error); \
		CHECK_DWARF_RESULT(res); \
		if (res == DW_DLV_NO_ENTRY) break; \
		if (cur_die != child) { \
			dwarf_dealloc(dbg, cur_die, DW_DLA_DIE); \
		} \
		cur_die = sib_die; \
	} \
}

#define HANDLE_ATTRIBUTES(die, cases) { \
	Dwarf_Attribute *attrs; \
	Dwarf_Signed numAttrs; \
	res = dwarf_attrlist(die, &attrs, &numAttrs, &error); \
	if (res == DW_DLV_OK) { \
		for (int i = 0; i < numAttrs; i++) { \
			Dwarf_Attribute attr = attrs[i]; \
			Dwarf_Half attype; \
			res = dwarf_whatattr(attr, &attype, &error); \
			CHECK_DWARF_RESULT(res); \
			switch (attype) { \
				cases \
				default: break; \
			} \
			dwarf_dealloc(dbg, attr, DW_DLA_ATTR); \
		} \
		dwarf_dealloc(dbg, attrs, DW_DLA_LIST); \
	} \
}

/* TODO optimize & add error checking */
#define ARRAY_PUSH_BACK(array, size, elem) do { \
	array = realloc(array, ++size * sizeof(*array)); \
	array[size - 1] = elem; \
} while (0)

static TcdType *makePlaceholder(uint64_t typeOffset) {
	uint64_t *placeholder = malloc(8);
	*placeholder = typeOffset;
	return (TcdType*)placeholder;
}

static void replacePlaceholder(TcdType **ptr, uint64_t *typeIds, uint32_t numTypeIds, TcdCompUnit *cu) {
	uint64_t id = *(uint64_t*)*ptr;
	free(*ptr);
	for (int i = 0; i < numTypeIds; i++) {
		if (typeIds[i] == id) {
			*ptr = &cu->types[i];
			return;
		}
	}
}

static int loadLocal(Dwarf_Debug dbg, Dwarf_Die die, TcdLocal *oLocal) {
	const int ErrorCode = TCDE_LOAD_LOCAL;
	Dwarf_Error error;
	int res;
	TcdLocal local = {0};
	HANDLE_ATTRIBUTES(die,
		case DW_AT_name: {
			char *data;
			res = dwarf_formstring(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			local.name = strdup(data); /* TODO should this be duplicated? */
		} break;
		case DW_AT_location: {
			Dwarf_Ptr data;
			Dwarf_Unsigned size;
			res = dwarf_formexprloc(attr, &size, &data, &error);
			if (res == DW_DLV_ERROR) break;
			local.locdesc.expr = malloc(size + 1);
			memcpy(local.locdesc.expr, data, size);
			local.locdesc.expr[size] = 0;
			/* dwarf_dealloc(dbg, data, DW_DLA_PTR); */
		} break;
		case DW_AT_type: {
			Dwarf_Off offset;
			res = dwarf_formref(attr, &offset, &error);
			if (res == DW_DLV_ERROR) break;
			local.type = makePlaceholder(offset);
		} break;
	)
	*oLocal = local;
	return TCDE_OK;
}

static int loadFunction(Dwarf_Debug dbg, Dwarf_Die die, TcdFunction *oFunc) {
	const int ErrorCode = TCDE_LOAD_FUNCTION;
	Dwarf_Error error;
	int res;
	TcdFunction func = {0};
	HANDLE_ATTRIBUTES(die,
		case DW_AT_name: {
			char *data;
			res = dwarf_formstring(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			func.name = strdup(data); /* TODO should this be duplicated? */
		} break;
		case DW_AT_low_pc: {
			Dwarf_Addr data;
			res = dwarf_formaddr(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			func.begin = data;
		} break;
		case DW_AT_high_pc: {
			Dwarf_Unsigned data;
			res = dwarf_formudata(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			func.end = data;
		} break;
	)
	func.end += func.begin;

	HANDLE_SUB_DIES(die,
		/* Load locals */
		case DW_TAG_variable:
		case DW_TAG_formal_parameter: {
			TcdLocal local;
			res = loadLocal(dbg, cur_die, &local);
			CHECK_LOAD_RESULT(res);
			ARRAY_PUSH_BACK(func.locals, func.numLocals, local);
		} break;
	)
	*oFunc = func;
	return TCDE_OK;
}

static int loadBaseType(Dwarf_Debug dbg, Dwarf_Die die, TcdType *oType, uint64_t *oTypeId) {
	const int ErrorCode = TCDE_LOAD_TYPE;
	Dwarf_Error error;
	int res;
	TcdType type = {0};
	type.tclass = TCDT_BASE;
	/* Fetch die offset */
	Dwarf_Off typeId;
	res = dwarf_dieoffset(die, &typeId, &error);
	CHECK_DWARF_RESULT(res);
	/* Load attributes */
	HANDLE_ATTRIBUTES(die,
		case DW_AT_name: {
			char *data;
			res = dwarf_formstring(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			type.as.base.name = strdup(data); /* TODO should this be duplicated? */
		} break;
		case DW_AT_byte_size: {
			Dwarf_Unsigned data;
			res = dwarf_formudata(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			type.size = data;
		} break;
		case DW_AT_encoding: {
			Dwarf_Unsigned data;
			res = dwarf_formudata(attr, &data, &error);
			CHECK_DWARF_RESULT(res);
			/* Convert dwarf encoding to tcd interpretation */
			switch (data) {
				case DW_ATE_address:       type.as.base.interp = TCDI_ADDRESS;  break;
				case DW_ATE_signed:        type.as.base.interp = TCDI_SIGNED;   break;
				case DW_ATE_unsigned:      type.as.base.interp = TCDI_UNSIGNED; break;
				case DW_ATE_signed_char:   type.as.base.interp = TCDI_CHAR;     break;
				case DW_ATE_unsigned_char: type.as.base.interp = TCDI_UCHAR;    break;
				case DW_ATE_float:         type.as.base.interp = TCDI_FLOAT;    break;
				case DW_ATE_boolean:       type.as.base.interp = TCDI_BOOL;     break;
			}
		} break;
	)
	*oType = type;
	*oTypeId = typeId;
	return 0;
}

static int loadPointerType(Dwarf_Debug dbg, Dwarf_Die die, TcdType *oType, uint64_t *oTypeId) {
	const int ErrorCode = TCDE_LOAD_TYPE;
	Dwarf_Error error;
	int res;
	TcdType type = {0};
	type.tclass = TCDT_POINTER;
	type.size = 8;
	/* Fetch type offset */
	Dwarf_Off typeId;
	res = dwarf_dieoffset(die, &typeId, &error);
	CHECK_DWARF_RESULT(res);
	/* Load attributes */
	HANDLE_ATTRIBUTES(die,
		case DW_AT_type: {
			Dwarf_Off to;
			res = dwarf_formref(attr, &to, &error);
			CHECK_DWARF_RESULT(res);
			type.as.pointer.to = makePlaceholder(to);
		} break;
	)
	*oType = type;
	*oTypeId = typeId;
	return TCDE_OK;
}

static int loadArrayType(Dwarf_Debug dbg, Dwarf_Die die, TcdType *oType, uint64_t *oTypeId) {
	const int ErrorCode = TCDE_LOAD_TYPE;
	Dwarf_Error error;
	int res;
	TcdType type = {0};
	type.tclass = TCDT_ARRAY;
	type.size = 0; /* TODO */
	/* Fetch type offset */
	Dwarf_Off typeId;
	res = dwarf_dieoffset(die, &typeId, &error);
	CHECK_DWARF_RESULT(res);
	/* Load attributes */
	HANDLE_ATTRIBUTES(die,
		case DW_AT_type: {
			Dwarf_Off of;
			res = dwarf_formref(attr, &of, &error);
			CHECK_DWARF_RESULT(res);
			type.as.array.of = makePlaceholder(of);
		} break;
	)
	*oType = type;
	*oTypeId = typeId;
	return TCDE_OK;
}

static int loadLines(Dwarf_Debug dbg, Dwarf_Die die, TcdCompUnit *cu) {
	const int ErrorCode = TCDE_LOAD_LINES;
	Dwarf_Error error;
	int res;
	TcdFunction *curFunc = cu->funcs;
	/* Fetch line list */
	Dwarf_Line *dlines;
	Dwarf_Signed dnumLines;
	res = dwarf_srclines(die, &dlines, &dnumLines, &error);
	if (res != DW_DLV_OK) return -1;
	/* For every line ... */
	uint32_t lastNumber = 0;
	for (uint32_t i = 0; i < dnumLines && curFunc != NULL; i++) {
		/* Fetch line number */
		Dwarf_Unsigned number;
		res = dwarf_lineno(dlines[i], &number, &error);
		CHECK_DWARF_RESULT(res);
		/* Fetch line address */
		Dwarf_Addr address;
		res = dwarf_lineaddr(dlines[i], &address, &error);
		CHECK_DWARF_RESULT(res);
		/* Advance to next function if neccessary */
		if (address >= curFunc->end) {
			curFunc++;
			if (curFunc >= cu->funcs + cu->numFuncs)
				curFunc = NULL;
		}
		/* Do not allow multiple addresses per line & lines without surrounding functions */
		if (number != lastNumber && curFunc != NULL) {
			TcdLine line = {number, address};
			ARRAY_PUSH_BACK(curFunc->lines, curFunc->numLines, line);
		}
		lastNumber = number;
		dwarf_dealloc(dbg, dlines[i], DW_DLA_LINE);
	}
	/* Deallocate line list */
	dwarf_dealloc(dbg, dlines, DW_DLA_LIST);
	/* Remove first (?) line in every function, as it seems to point into a weird limbo */
	for (int i = 0; i < cu->numFuncs; i++) {
		TcdFunction *f = &cu->funcs[i];
		memmove(f->lines, f->lines + 1, --f->numLines * sizeof(*f->lines));
		f->lines = realloc(f->lines, f->numLines * sizeof(*f->lines));
	}
	return 0;
}

int tcdLoadInfo(const char *file, TcdInfo *out_info) {
	TcdInfo info = {0};
	Dwarf_Debug dbg = 0;
	Dwarf_Error error;
	int res;
	Dwarf_Handler errhand = 0;
	Dwarf_Ptr errarg = 0;
	int fd = open(file, O_RDONLY);
	if (fd < 0) return TCDE_LOAD_OPEN;
	res = dwarf_init(fd, DW_DLC_READ, errhand, errarg, &dbg, &error);
	if (res != DW_DLV_OK) return TCDE_LOAD_INFO;

	int cu_number = 0;
	Dwarf_Unsigned cu_header_length = 0;
	Dwarf_Half     version_stamp    = 0;
	Dwarf_Unsigned abbrev_offset    = 0;
	Dwarf_Half     address_size     = 0;
	Dwarf_Unsigned next_cu_header   = 0;
	for (;;) {
		const int ErrorCode = TCDE_LOAD_COMP_UNIT;

		uint64_t *typeIds = NULL;
		uint32_t numTypeIds = 0;

		res = dwarf_next_cu_header(dbg, &cu_header_length,
			&version_stamp, &abbrev_offset, &address_size,
			&next_cu_header, &error);
		CHECK_DWARF_RESULT(res);
		if (res == DW_DLV_NO_ENTRY) break;
		/* The CU will have a single sibling, a cu_die. */
		Dwarf_Die cu_die = 0;
		res = dwarf_siblingof(dbg, 0, &cu_die, &error);
		CHECK_DWARF_RESULT(res);
		if (res == DW_DLV_NO_ENTRY) break; /* "Impossible" */
		/* Load compilation unit */
		TcdCompUnit cu = {0};
		/* Load compilation unit attributes */
		HANDLE_ATTRIBUTES(cu_die,
			case DW_AT_name: {
				char *data;
				res = dwarf_formstring(attr, &data, &error);
				CHECK_DWARF_RESULT(res);
				cu.name = strdup(data); /* TODO should this be duplicated? */
			} break;
			case DW_AT_comp_dir: {
				char *data;
				res = dwarf_formstring(attr, &data, &error);
				CHECK_DWARF_RESULT(res);
				cu.compDir = strdup(data); /* TODO should this be duplicated? */
			} break;
			case DW_AT_producer: {
				char *data;
				res = dwarf_formstring(attr, &data, &error);
				CHECK_DWARF_RESULT(res);
				cu.producer = strdup(data); /* TODO should this be duplicated? */
			} break;
			case DW_AT_low_pc: {
				Dwarf_Unsigned data;
				res = dwarf_formudata(attr, &data, &error);
				CHECK_DWARF_RESULT(res);
				cu.begin = data;
			} break;
			case DW_AT_high_pc: {
				Dwarf_Unsigned data;
				res = dwarf_formudata(attr, &data, &error);
				CHECK_DWARF_RESULT(res);
				cu.end = data;
			} break;
		)
		cu.end += cu.begin;

		/* Load all types, functions etc. */
		HANDLE_SUB_DIES(cu_die,
			/* Load function */
			case DW_TAG_subprogram: {
				TcdFunction func;
				res = loadFunction(dbg, cur_die, &func);
				CHECK_LOAD_RESULT(res);
				ARRAY_PUSH_BACK(cu.funcs, cu.numFuncs, func);
			} break;
			/* Load base type */
			case DW_TAG_base_type: {
				uint64_t typeId;
				TcdType type;
				res = loadBaseType(dbg, cur_die, &type, &typeId);
				CHECK_LOAD_RESULT(res);
				ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
				ARRAY_PUSH_BACK(typeIds, numTypeIds, typeId);
			} break;
			/* Load pointer type */
			case DW_TAG_pointer_type: {
				uint64_t typeId;
				TcdType type;
				res = loadPointerType(dbg, cur_die, &type, &typeId);
				CHECK_LOAD_RESULT(res);
				ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
				ARRAY_PUSH_BACK(typeIds, numTypeIds, typeId);
			} break;
			/* Load array type */
			case DW_TAG_array_type: {
				uint64_t typeId;
				TcdType type;
				res = loadArrayType(dbg, cur_die, &type, &typeId);
				CHECK_LOAD_RESULT(res);
				ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
				ARRAY_PUSH_BACK(typeIds, numTypeIds, typeId);
			} break;
		)

		/* Load lines */
		res = loadLines(dbg, cu_die, &cu);
		CHECK_LOAD_RESULT(res);

		/* Replace type offsets / placeholders by pointers */
		for (int i = 0; i < cu.numTypes; i++) {
			switch (cu.types[i].tclass) {
				case TCDT_POINTER:
					replacePlaceholder(&cu.types[i].as.pointer.to, typeIds, numTypeIds, &cu);
					break;
				case TCDT_ARRAY:
					replacePlaceholder(&cu.types[i].as.array.of, typeIds, numTypeIds, &cu);
					break;
				default: break;
			}
		}
		for (int i = 0; i < cu.numFuncs; i++) {
			for (int j = 0; j < cu.funcs[i].numLocals; j++) {
				replacePlaceholder(&cu.funcs[i].locals[j].type, typeIds, numTypeIds, &cu);
			}
		}

		/* Deallocate compilation unit die */
		dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);

		/* Add compilation unit to list */
		ARRAY_PUSH_BACK(info.compUnits, info.numCompUnits, cu);
		cu_number++;
	}
	/* Close dwarf handle */
	res = dwarf_finish(dbg, &error);
	if (res != DW_DLV_OK) {
		printf("dwarf_finish failed!\n");
	}
	/* Close executable's file handle */
	close(fd);
	/* Return info */
	*out_info = info;
	return 0;
}
