#include "tcd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>

#define CHECK_DWARF_RESULT(x) if (res == DW_DLV_ERROR) return -1

#define HANDLE_SUB_DIES(in_die, cases) { \
	Dwarf_Die cur_die = in_die; \
	while (1) { \
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
        if (cur_die != in_die) { \
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
			Dwarf_Half attype; \
			res = dwarf_whatattr(attrs[i], &attype, &error); \
			CHECK_DWARF_RESULT(res); \
			switch (attype) { \
				cases \
				default: break; \
			} \
			dwarf_dealloc(dbg, attrs[i], DW_DLA_ATTR); \
		} \
		dwarf_dealloc(dbg, attrs, DW_DLA_LIST); \
	} \
}

#define ATTR_GET_STRING(out) { \
	char *data; \
	res = dwarf_formstring(attrs[i], &data, &error); \
	CHECK_DWARF_RESULT(res); \
	out = strdup(data); \
	dwarf_dealloc(dbg, data, DW_DLA_STRING); \
}

#define ATTR_GET_UNSIGNED(out) { \
	Dwarf_Unsigned data; \
	res = dwarf_formudata(attrs[i], &data, &error); \
	CHECK_DWARF_RESULT(res); \
	out = data; \
}

#define ATTR_GET_ADDR(out) { \
	Dwarf_Addr data; \
	res = dwarf_formaddr(attrs[i], &data, &error); \
	CHECK_DWARF_RESULT(res); \
	out = data; \
}

/* TODO optimize & add error checking */
#define ARRAY_PUSH_BACK(array, size, elem) do { \
	array = realloc(array, ++size * sizeof(*array)); \
	array[size - 1] = elem; \
} while (0)

static int loadLocals(Dwarf_Debug dbg, Dwarf_Die in_die, TcdLocal **out_locals, uint32_t *out_numLocals) {
	TcdLocal *locals = NULL;
	uint32_t numLocals = 0;
	Dwarf_Error error;
	int res;
	HANDLE_SUB_DIES(in_die,
		case DW_TAG_variable: case DW_TAG_formal_parameter: {
			/* Found local */
			TcdLocal local = {0};
			HANDLE_ATTRIBUTES(cur_die,
				case DW_AT_name:
					ATTR_GET_STRING(local.name);
					break;
				case DW_AT_location: {
					Dwarf_Ptr data;
					Dwarf_Unsigned size;
					res = dwarf_formexprloc(attrs[i], &size, &data, &error);
					CHECK_DWARF_RESULT(res);
					local.exprloc = malloc(size + 1);
					memcpy(local.exprloc, data, size);
					local.exprloc[size] = 0;
					/* dwarf_dealloc(dbg, data, DW_DLA_PTR); */
				} break;
				case DW_AT_type: {
					Dwarf_Off data;
					res = dwarf_formref(attrs[i], &data, &error);
					CHECK_DWARF_RESULT(res);
					Dwarf_Die type;
					res = dwarf_offdie(dbg, data, &type, &error);
					CHECK_DWARF_RESULT(res);
					Dwarf_Half tag;
					res = dwarf_tag(type, &tag, &error);
					CHECK_DWARF_RESULT(res);
					const char *tagname;
					res = dwarf_get_TAG_name(tag, &tagname);
					printf("type die tag: %s\n", tagname + 7);
				} break;
			)
			ARRAY_PUSH_BACK(locals, numLocals, local);
		} break;
	)
	*out_locals = locals;
	*out_numLocals = numLocals;
	return 0;
}

static int loadLines(Dwarf_Debug dbg, Dwarf_Die die, TcdFunction *funcs, uint32_t numFuncs) {
	Dwarf_Error error;
	int res;
	TcdFunction *cur_func = funcs;
	uint32_t cur_func_index = 0;
	/* Fetch line list */
	Dwarf_Line *dlines;
	Dwarf_Signed dnumLines;
	res = dwarf_srclines(die, &dlines, &dnumLines, &error);
	if (res != DW_DLV_OK) return -1;
	/* For every line ... */
	uint32_t lastNumber = 0;
	for (uint32_t i = 0; i < dnumLines && cur_func != NULL; i++) {
		/* Fetch line number */
		Dwarf_Unsigned number;
		res = dwarf_lineno(dlines[i], &number, &error);
		CHECK_DWARF_RESULT(res);
		/* Fetch line address */
		Dwarf_Addr address;
		res = dwarf_lineaddr(dlines[i], &address, &error);
		CHECK_DWARF_RESULT(res);
		/* Advance to next function if neccessary */
		if (address >= cur_func->end) {
			cur_func++;
			cur_func_index++;
			if (cur_func_index >= numFuncs)
				cur_func = NULL;
		}
		/* Do not allow multiple addresses per line & lines without surrounding functions */
		if (lastNumber != number && cur_func != NULL) {
			TcdLine line = {number, address};
			ARRAY_PUSH_BACK(cur_func->lines, cur_func->numLines, line);
		}
		lastNumber = number;
		dwarf_dealloc(dbg, dlines[i], DW_DLA_LINE);
	}
	/* Remove first (?) line in every function */
	for (uint32_t i = 0; i < numFuncs; i++) {
		memmove(funcs[i].lines, funcs[i].lines + 1, --funcs[i].numLines * sizeof(*funcs[i].lines));
		funcs[i].lines = realloc(funcs[i].lines, funcs[i].numLines * sizeof(*funcs[i].lines));
	}
	/* Deallocate line list */
	dwarf_dealloc(dbg, dlines, DW_DLA_LIST);
	return 0;
}

static int loadCU(Dwarf_Debug dbg, Dwarf_Die cu_die, TcdCompUnit *out_cu)
{
	Dwarf_Error error;
	int res;
	TcdCompUnit cu = {0};
	/* Load compilation unit attributes */
	HANDLE_ATTRIBUTES(cu_die,
		case DW_AT_name:     ATTR_GET_STRING  (cu.name    ); break;
		case DW_AT_comp_dir: ATTR_GET_STRING  (cu.compDir ); break;
		case DW_AT_producer: ATTR_GET_STRING  (cu.producer); break;
		case DW_AT_low_pc:   ATTR_GET_ADDR    (cu.begin   ); break;
		case DW_AT_high_pc:  ATTR_GET_UNSIGNED(cu.end     ); break;
	)
	cu.end += cu.begin;
	/* Load all sub dies */
	Dwarf_Die child;
	res = dwarf_child(cu_die, &child, &error);
	CHECK_DWARF_RESULT(res);
	HANDLE_SUB_DIES(child,
		case DW_TAG_subprogram: {
			/* Found function */
			TcdFunction func = {0};
			HANDLE_ATTRIBUTES(cur_die,
				case DW_AT_name:    ATTR_GET_STRING  (func.name ); break;
				case DW_AT_low_pc:  ATTR_GET_ADDR    (func.begin); break;
				case DW_AT_high_pc: ATTR_GET_UNSIGNED(func.end  ); break;
			)
			func.end += func.begin;

			Dwarf_Die child;
			res = dwarf_child(cur_die, &child, &error);
			CHECK_DWARF_RESULT(res);
			if (loadLocals(dbg, child, &func.locals, &func.numLocals) != 0)
				return -1;
			ARRAY_PUSH_BACK(cu.funcs, cu.numFuncs, func);
		} break;
		case DW_TAG_base_type: {
			TcdType type = {0};
			type.tclass = TCDT_BASE;
			/* Fetch die offset / type id */
			Dwarf_Off offset;
			res = dwarf_dieoffset(cur_die, &offset, &error);
			CHECK_DWARF_RESULT(res);
			type.base.off = offset;
			/* Iterate trough attributes */
			HANDLE_ATTRIBUTES(cur_die,
				case DW_AT_name:      ATTR_GET_STRING  (type.base.name); break;
				case DW_AT_byte_size: ATTR_GET_UNSIGNED(type.base.size); break;
				case DW_AT_encoding: {
					Dwarf_Unsigned inter;
					ATTR_GET_UNSIGNED(inter);
					switch (inter) {
						case DW_ATE_address:       type.base.inter = TCDI_ADDRESS;  break;
						case DW_ATE_signed:        type.base.inter = TCDI_SIGNED;   break;
						case DW_ATE_unsigned:      type.base.inter = TCDI_UNSIGNED; break;
						case DW_ATE_signed_char:   type.base.inter = TCDI_CHAR;     break;
						case DW_ATE_unsigned_char: type.base.inter = TCDI_UCHAR;    break;
						case DW_ATE_float:         type.base.inter = TCDI_FLOAT;    break;
						case DW_ATE_boolean:       type.base.inter = TCDI_BOOL;     break;
					}
				} break;
			)
			ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
		} break;
		case DW_TAG_pointer_type: {
			TcdType type = {0};
			type.tclass = TCDT_POINTER;
			HANDLE_ATTRIBUTES(cur_die,
				case DW_AT_type: {
					Dwarf_Off data;
					res = dwarf_formref(attrs[i], &data, &error);
					CHECK_DWARF_RESULT(res);
					type.pointer.off = data;
				} break;
			)
			ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
		} break;
	)
	/* Load lines */
	if (loadLines(dbg, cu_die, cu.funcs, cu.numFuncs) != 0)
		return -1;
	*out_cu = cu;
    return 0;
}

int tcdLoadInfo(const char *file, TcdInfo *out_info) {
	TcdInfo info = {0};
	Dwarf_Debug dbg = 0;
	Dwarf_Error error;
    Dwarf_Handler errhand = 0;
    Dwarf_Ptr errarg = 0;
	int res;
    int fd = open(file, O_RDONLY);
    if (fd < 0) {
        printf("Failure attempting to open %s\n", file);
		return -1;
    }
    res = dwarf_init(fd, DW_DLC_READ, errhand, errarg, &dbg, &error);
    if (res != DW_DLV_OK) {
        printf("Giving up, cannot do DWARF processing\n");
        return -1;
    }

	Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Half     version_stamp    = 0;
    Dwarf_Unsigned abbrev_offset    = 0;
    Dwarf_Half     address_size     = 0;
    Dwarf_Unsigned next_cu_header   = 0;
	int cu_number = 0;
    while (1) {
		TcdCompUnit cu = {0};
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
		if (loadCU(dbg, cu_die, &cu) != 0)
			return -1;
        dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
		/* Add compilation unit to list */
		ARRAY_PUSH_BACK(info.compUnits, info.numCompUnits, cu);
		cu_number++;
    }
	res = dwarf_finish(dbg, &error);
    if (res != DW_DLV_OK) {
        printf("dwarf_finish failed!\n");
    }
    close(fd);
	*out_info = info;
	return 0;
}
