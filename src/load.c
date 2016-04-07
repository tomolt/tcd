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

static int loadLocals(Dwarf_Debug dbg, Dwarf_Die in_die, TcdLocal **out_locals, uint32_t *out_numLocals) {
	TcdLocal *locals = NULL;
	uint32_t numLocals = 0;
	Dwarf_Die cur_die = in_die;
	Dwarf_Error error;
	int res;
	while (1) {
		Dwarf_Half tag;
		res = dwarf_tag(cur_die, &tag, &error);
		CHECK_DWARF_RESULT(res);
		if (tag == DW_TAG_variable || tag == DW_TAG_formal_parameter) {
			/* Found local */
			TcdLocal local = {0};
			Dwarf_Attribute *attrs;
			Dwarf_Signed numAttrs;
			res = dwarf_attrlist(cur_die, &attrs, &numAttrs, &error);
			if (res == DW_DLV_OK) {
				for (int i = 0; i < numAttrs; i++) {
					Dwarf_Half attr;
					res = dwarf_whatattr(attrs[i], &attr, &error);
					CHECK_DWARF_RESULT(res);
					switch (attr) {
						case DW_AT_name: {
							char *data;
							res = dwarf_formstring(attrs[i], &data, &error);
							CHECK_DWARF_RESULT(res);
							local.name = strdup(data);
							dwarf_dealloc(dbg, data, DW_DLA_STRING);
						} break;
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
					}
					dwarf_dealloc(dbg, attrs[i], DW_DLA_ATTR);
				}
				dwarf_dealloc(dbg, attrs, DW_DLA_LIST);
			}
			locals = realloc(locals, ++numLocals * sizeof(*locals));
			locals[numLocals - 1] = local;
		}
		Dwarf_Die sib_die = 0;
        res = dwarf_siblingof(dbg, cur_die, &sib_die, &error);
        CHECK_DWARF_RESULT(res);
        if (res == DW_DLV_NO_ENTRY)
			break;
        if (cur_die != in_die) {
            dwarf_dealloc(dbg, cur_die, DW_DLA_DIE);
        }
        cur_die = sib_die;
	}
	*out_locals = locals;
	*out_numLocals = numLocals;
	return 0;
}

static int loadFuncs(Dwarf_Debug dbg, Dwarf_Die in_die, TcdFunction **out_funcs, uint32_t *out_numFuncs) {
	TcdFunction *funcs = NULL;
	uint32_t numFuncs = 0;
	Dwarf_Die cur_die = in_die;
	Dwarf_Error error;
	int res;
	while (1) {
		Dwarf_Half tag;
		res = dwarf_tag(cur_die, &tag, &error);
		CHECK_DWARF_RESULT(res);
		if (tag == DW_TAG_subprogram) {
			/* Found function */
			TcdFunction func = {0};
			Dwarf_Attribute *attrs;
			Dwarf_Signed numAttrs;
			res = dwarf_attrlist(cur_die, &attrs, &numAttrs, &error);
			if (res == DW_DLV_OK) {
				for (int i = 0; i < numAttrs; i++) {
					Dwarf_Half attr;
					res = dwarf_whatattr(attrs[i], &attr, &error);
					CHECK_DWARF_RESULT(res);
					Dwarf_Half form;
					res = dwarf_whatform(attrs[i], &form, &error);
					CHECK_DWARF_RESULT(res);
					switch (attr) {
						case DW_AT_name: {
							char *data;
							res = dwarf_formstring(attrs[i], &data, &error);
							CHECK_DWARF_RESULT(res);
							func.name = strdup(data);
							dwarf_dealloc(dbg, data, DW_DLA_STRING);
						} break;
						case DW_AT_low_pc: {
							Dwarf_Addr data;
							res = dwarf_formaddr(attrs[i], &data, &error);
							CHECK_DWARF_RESULT(res);
							func.begin = data;
						} break;
						case DW_AT_high_pc: {
							Dwarf_Unsigned data;
							res = dwarf_formudata(attrs[i], &data, &error);
							CHECK_DWARF_RESULT(res);
							func.end = data;
						} break;
					}
					dwarf_dealloc(dbg, attrs[i], DW_DLA_ATTR);
				}
				dwarf_dealloc(dbg, attrs, DW_DLA_LIST);
			}
			func.end += func.begin;

			Dwarf_Die child;
			res = dwarf_child(cur_die, &child, &error);
			CHECK_DWARF_RESULT(res);
			if (loadLocals(dbg, child, &func.locals, &func.numLocals) != 0)
				return -1;

			funcs = realloc(funcs, ++numFuncs * sizeof(*funcs));
			funcs[numFuncs - 1] = func;
		}
		Dwarf_Die sib_die = 0;
        res = dwarf_siblingof(dbg, cur_die, &sib_die, &error);
        CHECK_DWARF_RESULT(res);
        if (res == DW_DLV_NO_ENTRY)
			break;
        if (cur_die != in_die) {
            dwarf_dealloc(dbg, cur_die, DW_DLA_DIE);
        }
        cur_die = sib_die;
	}
	*out_funcs = funcs;
	*out_numFuncs = numFuncs;
	return 0;
}

static int loadTypes(Dwarf_Debug dbg, Dwarf_Die in_die, TcdType **out_types, uint32_t *out_numTypes) {
	TcdType *types = NULL;
	uint32_t numTypes = 0;
	Dwarf_Error error;
	Dwarf_Die cur_die = in_die;
	int res;
	while (1) {
		TcdType type = {0};
		Dwarf_Half tag;
		res = dwarf_tag(cur_die, &tag, &error);
		CHECK_DWARF_RESULT(res);
		switch (tag) {
			case DW_TAG_base_type: {
				type.tclass = TCDT_BASE;
				Dwarf_Attribute *attrs;
				Dwarf_Signed numAttrs;
				res = dwarf_attrlist(cur_die, &attrs, &numAttrs, &error);
				if (res == DW_DLV_OK) {
					for (int i = 0; i < numAttrs; i++) {
						Dwarf_Half attr;
						res = dwarf_whatattr(attrs[i], &attr, &error);
						CHECK_DWARF_RESULT(res);
						switch (attr) {
							case DW_AT_name: {
								char *data;
								res = dwarf_formstring(attrs[i], &data, &error);
								CHECK_DWARF_RESULT(res);
								type.base.name = strdup(data);
								dwarf_dealloc(dbg, data, DW_DLA_STRING);
							} break;
							case DW_AT_byte_size: {
								Dwarf_Unsigned data;
								res = dwarf_formudata(attrs[i], &data, &error);
								CHECK_DWARF_RESULT(res);
								type.base.size = data;
							} break;
							case DW_AT_encoding: {
								Dwarf_Unsigned data;
								res = dwarf_formudata(attrs[i], &data, &error);
								CHECK_DWARF_RESULT(res);
								switch (data) {
									case DW_ATE_address:       type.base.inter = TCDI_ADDRESS;  break;
									case DW_ATE_signed:        type.base.inter = TCDI_SIGNED;   break;
									case DW_ATE_unsigned:      type.base.inter = TCDI_UNSIGNED; break;
									case DW_ATE_signed_char:   type.base.inter = TCDI_CHAR;     break;
									case DW_ATE_unsigned_char: type.base.inter = TCDI_UCHAR;    break;
									case DW_ATE_float:         type.base.inter = TCDI_FLOAT;    break;
									case DW_ATE_boolean:       type.base.inter = TCDI_BOOL;     break;
								}
							} break;
						}
						dwarf_dealloc(dbg, attrs[i], DW_DLA_ATTR);
					}
					dwarf_dealloc(dbg, attrs, DW_DLA_LIST);
				}
			} break;
			case DW_TAG_pointer_type: {
				type.tclass = TCDT_POINTER;
				Dwarf_Attribute *attrs;
				Dwarf_Signed numAttrs;
				res = dwarf_attrlist(cur_die, &attrs, &numAttrs, &error);
				if (res == DW_DLV_OK) {
					for (int i = 0; i < numAttrs; i++) {
						Dwarf_Half attr;
						res = dwarf_whatattr(attrs[i], &attr, &error);
						CHECK_DWARF_RESULT(res);
						switch (attr) {
							case DW_AT_type: {
							} break;
						}
						dwarf_dealloc(dbg, attrs[i], DW_DLA_ATTR);
					}
					dwarf_dealloc(dbg, attrs, DW_DLA_LIST);
				}
			} break;
		}
		types = realloc(types, ++numTypes * sizeof(*types));
		types[numTypes - 1] = type;

		Dwarf_Die sib_die = 0;
        res = dwarf_siblingof(dbg, cur_die, &sib_die, &error);
        CHECK_DWARF_RESULT(res);
        if (res == DW_DLV_NO_ENTRY)
			break;
        if (cur_die != in_die) {
            dwarf_dealloc(dbg, cur_die, DW_DLA_DIE);
        }
        cur_die = sib_die;
	}
	*out_types = types;
	*out_numTypes = numTypes;
	return 0;
}

static int loadLines(Dwarf_Debug dbg, Dwarf_Die die, TcdFunction *funcs, uint32_t numFuncs) {
	Dwarf_Error error;
	int res;
	TcdFunction *cur_func = funcs;
	uint32_t cur_func_index = 0;
	Dwarf_Line *dlines;
	Dwarf_Signed dnumLines;
	res = dwarf_srclines(die, &dlines, &dnumLines, &error);
	if (res != DW_DLV_OK)
		return -1;
	uint32_t lastNumber = 0;
	for (uint32_t i = 0; i < dnumLines && cur_func != NULL; i++) {
		Dwarf_Unsigned number;
		Dwarf_Addr address;
		res = dwarf_lineno(dlines[i], &number, &error);
		CHECK_DWARF_RESULT(res);
		res = dwarf_lineaddr(dlines[i], &address, &error);
		CHECK_DWARF_RESULT(res);
		if (address >= cur_func->end) {
			cur_func++;
			cur_func_index++;
			if (cur_func_index >= numFuncs)
				cur_func = NULL;
		}
		if (lastNumber != number && cur_func != NULL) {
			cur_func->lines = realloc(cur_func->lines,
				++cur_func->numLines * sizeof(*cur_func->lines));
			cur_func->lines[cur_func->numLines - 1].number = number;
			cur_func->lines[cur_func->numLines - 1].address = address;
			dwarf_dealloc(dbg, dlines[i], DW_DLA_LINE);
		}
		lastNumber = number;
	}
	for (uint32_t i = 0; i < numFuncs; i++) {
		memmove(funcs[i].lines, funcs[i].lines + 1, --funcs[i].numLines * sizeof(*funcs[i].lines));
		funcs[i].lines = realloc(funcs[i].lines, funcs[i].numLines * sizeof(*funcs[i].lines));
	}
	dwarf_dealloc(dbg, dlines, DW_DLA_LIST);
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
		/* Load compilation unit attributes */
		Dwarf_Attribute *attrs;
		Dwarf_Signed numAttrs;
		res = dwarf_attrlist(cu_die, &attrs, &numAttrs, &error);
		if (res == DW_DLV_OK) {
			for (int i = 0; i < numAttrs; i++) {
				Dwarf_Half attr;
				res = dwarf_whatattr(attrs[i], &attr, &error);
				CHECK_DWARF_RESULT(res);
				switch (attr) {
					case DW_AT_name: {
						char *data;
						res = dwarf_formstring(attrs[i], &data, &error);
						CHECK_DWARF_RESULT(res);
						cu.name = strdup(data);
						dwarf_dealloc(dbg, data, DW_DLA_STRING);
					} break;
					case DW_AT_comp_dir: {
						char *data;
						res = dwarf_formstring(attrs[i], &data, &error);
						CHECK_DWARF_RESULT(res);
						cu.compDir = strdup(data);
						dwarf_dealloc(dbg, data, DW_DLA_STRING);
					} break;
					case DW_AT_producer: {
						char *data;
						res = dwarf_formstring(attrs[i], &data, &error);
						CHECK_DWARF_RESULT(res);
						cu.producer = strdup(data);
						dwarf_dealloc(dbg, data, DW_DLA_STRING);
					} break;
					case DW_AT_low_pc: {
						Dwarf_Addr data;
						res = dwarf_formaddr(attrs[i], &data, &error);
						CHECK_DWARF_RESULT(res);
						cu.begin = data;
					} break;
					case DW_AT_high_pc: {
						Dwarf_Unsigned data;
						res = dwarf_formudata(attrs[i], &data, &error);
						CHECK_DWARF_RESULT(res);
						cu.end = data;
					} break;
				}
				dwarf_dealloc(dbg, attrs[i], DW_DLA_ATTR);
			}
			dwarf_dealloc(dbg, attrs, DW_DLA_LIST);
		}
		cu.end += cu.begin;
		/* Load functions */
		Dwarf_Die child;
		res = dwarf_child(cu_die, &child, &error);
		CHECK_DWARF_RESULT(res);
		if (loadFuncs(dbg, child, &cu.funcs, &cu.numFuncs) != 0)
			return -1;
#if 0
		/* Load types */
		res = dwarf_child(cu_die, &child, &error);
		CHECK_DWARF_RESULT(res);
		if (load_types(dbg, child, &cu.bases, &cu.numBases) != 0)
			return -1;
#endif
		/* Load lines */
		if (loadLines(dbg, cu_die, cu.funcs, cu.numFuncs) != 0)
			return -1;

        dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
		info.compUnits = realloc(info.compUnits, ++info.numCompUnits * sizeof(*info.compUnits));
		info.compUnits[info.numCompUnits - 1] = cu;
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
