#include "tcd.h"

#include <stdlib.h>

void tcdFreeContext(TcdContext *debug) {
	tcdFreeInfo(&debug->info);
	free(debug->breaks);
}
