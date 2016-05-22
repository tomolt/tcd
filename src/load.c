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

#define HANDLE_SUB_DIES(parent, cases) { \
	Dwarf_Die child; \
	res = dwarf_child(parent, &child, &error); \
	CHECK_DWARF_RESULT(res); \
	Dwarf_Die cur_die = child; \
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

#define STORE_TYPE_OFFSET_IN_POINTER(ptr, off) do { \
	uint64_t *offset = malloc(8); \
	*offset = off; \
	ptr = (TcdType*)offset; \
} while (0)

/* TODO optimize & add error checking */
#define REPLACE_TYPE_OFFSET_BY_POINTER(ptr) do { \
	uint64_t id = *(uint64_t*)ptr; \
	free(ptr); \
	for (uint64_t k = 0; k < numTypeIds; k++) { \
		if (typeIds[k] == id) { \
			ptr = cu.types + k; \
			break; \
		} \
	} \
} while (0)

int tcdLoadInfo(const char *file, TcdInfo *out_info) {
	TcdInfo info = {0};
	Dwarf_Debug dbg = 0;
	Dwarf_Error error;
	int res;
    Dwarf_Handler errhand = 0;
    Dwarf_Ptr errarg = 0;
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
			case DW_AT_name:     ATTR_GET_STRING  (cu.name    ); break;
			case DW_AT_comp_dir: ATTR_GET_STRING  (cu.compDir ); break;
			case DW_AT_producer: ATTR_GET_STRING  (cu.producer); break;
			case DW_AT_low_pc:   ATTR_GET_ADDR    (cu.begin   ); break;
			case DW_AT_high_pc:  ATTR_GET_UNSIGNED(cu.end     ); break;
		)
		cu.end += cu.begin;

		uint64_t *typeIds = NULL;
		uint32_t numTypeIds = 0;
		/* Load all types, functions etc. */
		HANDLE_SUB_DIES(cu_die,
			/* Load function */
			case DW_TAG_subprogram: {
				TcdFunction func = {0};
				HANDLE_ATTRIBUTES(cur_die,
					case DW_AT_name:    ATTR_GET_STRING  (func.name ); break;
					case DW_AT_low_pc:  ATTR_GET_ADDR    (func.begin); break;
					case DW_AT_high_pc: ATTR_GET_UNSIGNED(func.end  ); break;
				)
				func.end += func.begin;
				/* Load locals */
				HANDLE_SUB_DIES(cur_die,
					case DW_TAG_variable: case DW_TAG_formal_parameter: {
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
								local.locdesc.expr = malloc(size + 1);
								memcpy(local.locdesc.expr, data, size);
								local.locdesc.expr[size] = 0;
								/* dwarf_dealloc(dbg, data, DW_DLA_PTR); */
							} break;
							case DW_AT_type: {
								Dwarf_Off off;
								res = dwarf_formref(attrs[i], &off, &error);
								CHECK_DWARF_RESULT(res);
								STORE_TYPE_OFFSET_IN_POINTER(local.type, off);
							} break;
						)
						ARRAY_PUSH_BACK(func.locals, func.numLocals, local);
					} break;
				)
				ARRAY_PUSH_BACK(cu.funcs, cu.numFuncs, func);
			} break;
			case DW_TAG_base_type: {
				TcdType type = {0};
				type.tclass = TCDT_BASE;
				/* Fetch die offset */
				Dwarf_Off typeId;
				res = dwarf_dieoffset(cur_die, &typeId, &error);
				CHECK_DWARF_RESULT(res);
				/* Load attributes */
				HANDLE_ATTRIBUTES(cur_die,
					case DW_AT_name:      ATTR_GET_STRING  (type.as.base.name); break;
					case DW_AT_byte_size: ATTR_GET_UNSIGNED(type.size); break;
					case DW_AT_encoding: {
						Dwarf_Unsigned inter;
						ATTR_GET_UNSIGNED(inter);
						/* Convert dwarf encoding to tcd interpretation */
						switch (inter) {
							case DW_ATE_address:       type.as.base.inter = TCDI_ADDRESS;  break;
							case DW_ATE_signed:        type.as.base.inter = TCDI_SIGNED;   break;
							case DW_ATE_unsigned:      type.as.base.inter = TCDI_UNSIGNED; break;
							case DW_ATE_signed_char:   type.as.base.inter = TCDI_CHAR;     break;
							case DW_ATE_unsigned_char: type.as.base.inter = TCDI_UCHAR;    break;
							case DW_ATE_float:         type.as.base.inter = TCDI_FLOAT;    break;
							case DW_ATE_boolean:       type.as.base.inter = TCDI_BOOL;     break;
						}
					} break;
				)
				ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
				ARRAY_PUSH_BACK(typeIds, numTypeIds, typeId);
			} break;
			case DW_TAG_pointer_type: {
				TcdType type = {0};
				type.tclass = TCDT_POINTER;
				type.size = 8;
				/* Fetch type offset */
				Dwarf_Off typeId;
				res = dwarf_dieoffset(cur_die, &typeId, &error);
				CHECK_DWARF_RESULT(res);
				/* Load attributes */
				HANDLE_ATTRIBUTES(cur_die,
					case DW_AT_type: {
						Dwarf_Off to;
						res = dwarf_formref(attrs[i], &to, &error);
						CHECK_DWARF_RESULT(res);
						STORE_TYPE_OFFSET_IN_POINTER(type.as.pointer.to, to);
					} break;
				)
				ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
				ARRAY_PUSH_BACK(typeIds, numTypeIds, typeId);
			} break;
			case DW_TAG_array_type: {
				TcdType type = {0};
				type.tclass = TCDT_ARRAY;
				type.size = 0; /* TODO */
				/* Fetch type offset */
				Dwarf_Off typeId;
				res = dwarf_dieoffset(cur_die, &typeId, &error);
				CHECK_DWARF_RESULT(res);
				/* Load attributes */
				HANDLE_ATTRIBUTES(cur_die,
					case DW_AT_type: {
						Dwarf_Off of;
						res = dwarf_formref(attrs[i], &of, &error);
						CHECK_DWARF_RESULT(res);
						STORE_TYPE_OFFSET_IN_POINTER(type.as.array.of, of);
					} break;
				)
				ARRAY_PUSH_BACK(cu.types, cu.numTypes, type);
				ARRAY_PUSH_BACK(typeIds, numTypeIds, typeId);
			} break;
		)
		/* Load lines */
		TcdFunction *cur_func = cu.funcs;
		uint32_t cur_func_index = 0;
		/* Fetch line list */
		Dwarf_Line *dlines;
		Dwarf_Signed dnumLines;
		res = dwarf_srclines(cu_die, &dlines, &dnumLines, &error);
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
				if (cur_func_index >= cu.numFuncs)
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
		for (uint32_t i = 0; i < cu.numFuncs; i++) {
			memmove(cu.funcs[i].lines, cu.funcs[i].lines + 1, --cu.funcs[i].numLines * sizeof(*cu.funcs[i].lines));
			cu.funcs[i].lines = realloc(cu.funcs[i].lines, cu.funcs[i].numLines * sizeof(*cu.funcs[i].lines));
		}
		/* Deallocate line list */
		dwarf_dealloc(dbg, dlines, DW_DLA_LIST);
		/* Replace type offsets / placeholders by pointers */
		for (int i = 0; i < cu.numTypes; i++) {
			switch (cu.types[i].tclass) {
				case TCDT_POINTER: REPLACE_TYPE_OFFSET_BY_POINTER(cu.types[i].as.pointer.to); break;
				case TCDT_ARRAY:   REPLACE_TYPE_OFFSET_BY_POINTER(cu.types[i].as.array.of  ); break;
				default: break;
			}
		}
		for (int i = 0; i < cu.numFuncs; i++) {
			for (int j = 0; j < cu.funcs[i].numLocals; j++) {
				REPLACE_TYPE_OFFSET_BY_POINTER(cu.funcs[i].locals[j].type);
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
