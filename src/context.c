#include "tcd.h"

#include <stdlib.h>

void tcdFreeContext(TcdContext *debug) {
	tcdFreeInfo(&debug->info);
	free(debug->breaks);
}

const char *tcdFormulateErrorMessage(int code) {
	switch (code) {
		case TCDE_OK: return "";
		case TCDE_LOAD_OPEN: return "Unable to open the file containing the debug information.";
		case TCDE_LOAD_INFO: return "Unable to load debug information.";
		case TCDE_LOAD_COMP_UNIT: return "Encountered corrupt debug information while loading compilation unit information.";
		case TCDE_LOAD_LINES: return "Encountered corrupt debug information while loading line number information.";
		case TCDE_LOAD_FUNCTION: return "Encountered corrupt debug information while loading function information.";
		case TCDE_LOAD_LOCAL: return "Encountered corrupt debug information while loading local variable information.";
		case TCDE_LOAD_TYPE: return "Encountered corrupt debug information while loading type information.";
		default: return "An unrecognized error has occured.";
	}
}
