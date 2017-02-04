#include "tcd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <readline/readline.h>

char prompt[128];

/* Debugger commands */
typedef enum {
	CONTINUE, BREAK,
	KILL,
	STEP, NEXT,
	TRACE, WHERE,
	REGISTERS, LINES, TYPES, LOCALS, POINTS,
	DUMP, PRINT,
	INVALID
} Command;

/* Reads command from user */
static void getNextCommand(Command *cmd, char *arg1, char *arg2) {
	char *cmdstr = readline(prompt);
	if (cmdstr == NULL) return;
	if (cmdstr[0] == '\0') return;
	char op[128];
	memset(op, 0, 128);
	memset(arg1, 0, 128);
	memset(arg2, 0, 128);

	int i;
	char *c = cmdstr;
	for (i = 0; *c != ' ' && *c != '\n' && *c != '\0'; c++)
		op[i++] = *c;
	while (*c == ' ') c++;
	for (i = 0; *c != ' ' && *c != '\n' && *c != '\0'; c++)
		arg1[i++] = *c;
	while (*c == ' ') c++;
	for (i = 0; *c != ' ' && *c != '\n' && *c != '\0'; c++)
		arg2[i++] = *c;

	if (strcmp(op, "continue") == 0) {
		*cmd = CONTINUE;
	} else if (strcmp(op, "kill") == 0) {
		*cmd = KILL;
	} else if (strcmp(op, "step") == 0) {
		*cmd = STEP;
	} else if (strcmp(op, "next") == 0) {
		*cmd = NEXT;
	} else if (strcmp(op, "trace") == 0) {
		*cmd = TRACE;
	} else if (strcmp(op, "where") == 0) {
		*cmd = WHERE;
	} else if (strcmp(op, "registers") == 0) {
		*cmd = REGISTERS;
	} else if (strcmp(op, "lines") == 0) {
		*cmd = LINES;
	} else if (strcmp(op, "types") == 0) {
		*cmd = TYPES;
	} else if (strcmp(op, "locals") == 0) {
		*cmd = LOCALS;
	} else if (strcmp(op, "points") == 0) {
		*cmd = POINTS;
	} else if (strcmp(op, "break") == 0) {
		*cmd = BREAK;
	} else if (strcmp(op, "dump") == 0) {
		*cmd = DUMP;
	} else if (strcmp(op, "print") == 0) {
		*cmd = PRINT;
	} else {
		*cmd = INVALID;
	}
	free(cmdstr);
}

static void printWhere(TcdInfo *info, uint64_t address) {
	printf("0x%lx", address);
	TcdFunction *func = tcdSurroundingFunction(info, address);
	if (func != NULL) {
		printf(", in function '%s'", func->name);
		TcdLine *line = tcdNearestLine(func, address);
		if (line != NULL) {
			printf(", line %d", line->number);
		}
	}
	printf(".\n");
}

static void typeToString(TcdType *type, char *str)
{
	switch (type->tclass) {
		case TCDT_BASE:
			strcpy(str, type->as.base.name);
			break;
		case TCDT_POINTER:
			str[0] = '*';
			typeToString(type->as.pointer.to, str + 1);
			break;
		case TCDT_ARRAY:
			str[0] = '[';
			str[1] = ']';
			typeToString(type->as.array.of, str + 2);
			break;
		default: break;
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <bin>\n", argv[0]);
		exit(-1);
	}

	const char *path = argv[1];
	const char *name = strrchr(path, '/');
	if (name) {
		name += 1;
	} else {
		name = path;
	}

	/* Init debug context */
	TcdContext debug = {0};
	if (tcdLoadInfo(path, &debug.info) != 0) {
		return -1;
	}

	switch (debug.pid = fork()) {
		case -1: { /* Error */
			perror("fork()");
		} return -1;
		case 0: { /* Child process */
			ptrace(PTRACE_TRACEME, NULL, NULL);     /* Allow child process to be traced */
			execl(path, name, NULL);                /* Child will be stopped here */
			perror("execl()");
		} return -1;
		/* Parent continues execution */
	}

	/* Set up prompt */
	sprintf(prompt, "tcd/%d] ", debug.pid);

	Command cmd;
	char arg1[128], arg2[128];

	tcdSync(&debug);

	while (1) {
		if (WIFEXITED(debug.status) || (WIFSIGNALED(debug.status) && WTERMSIG(debug.status) == SIGKILL)) {
			printf("process %d terminated\n", debug.pid);
			exit(0);
		}

		if (WIFSTOPPED(debug.status)) {
			uint64_t rip = tcdReadIP(&debug);
			/* Find breakpoint */
			uint64_t bip = rip - 1;
			int32_t index = -1;
			for (uint32_t i = 0; i < debug.numBreaks; i++) {
				if (debug.breaks[i].address == bip) {
					index = i;
					break;
				}
			}
			if (index != -1) {
				TcdBreakpoint point = debug.breaks[index];
				memmove(debug.breaks + index, debug.breaks + index + 1,
					(debug.numBreaks - index - 1) * sizeof(*debug.breaks));
				debug.breaks = realloc(debug.breaks, --debug.numBreaks * sizeof(*debug.breaks));
				/* Restore instruction(s) */
				tcdWriteMemory(&debug, point.address, 1, &point.saved);
				/* Decrement eip */
				ptrace(PTRACE_POKEUSER, debug.pid, 8 * RIP, bip);
				printf("Stopped [at breakpoint] at ");
				printWhere(&debug.info, bip);
			}
		}

		getNextCommand(&cmd, (char*)arg1, (char*)arg2);
		switch (cmd) {
			/* Set break point */
			case BREAK: {
				/* Find the address of the line */
				uint64_t address = 0;
				uint32_t line = 0;
				char symbol[256];
				if (sscanf(arg1, "%s", symbol) == 1) {
					TcdFunction *func = tcdFunctionByName(&debug.info, symbol);
					if (func != NULL) {
						address = func->lines[0].address;
						line = func->lines[0].number;
					} else {
						printf("Couldn't find function '%s'.\n", symbol);
					}
				} else {
					printf("Couldn't interpret breakpoint location.\n");
				}
				if (address != 0) {
					tcdInsertBreakpoint(&debug, address, line);
					printf("Set breakpoint at ");
					printWhere(&debug.info, address);
				} else {
					printf("line %d not found.\n", line);
				}
			} break;

			/* Step into */
			case STEP: {
				uint64_t rip = tcdStep(&debug);
				printf("Stepped to ");
				printWhere(&debug.info, rip);
			} break;

			/* Step over */
			case NEXT: {
				uint64_t rip = tcdNext(&debug);
				printf("Stepped to ");
				printWhere(&debug.info, rip);
			} break;

			/* Print stack trace */
			case TRACE: {
				uint64_t trace[128];
				uint16_t depth = tcdGetStackTrace(&debug, trace, 128);
				for (uint16_t level = 0; level < depth; level++) {
					printf("<%d> ", level);
					printWhere(&debug.info, trace[level]);
				}
			} break;

			/* Print current location */
			case WHERE: {
				uint64_t rip = tcdReadIP(&debug);
				printf("At ");
				printWhere(&debug.info, rip);
			} break;

			/* Dump registers */
			case REGISTERS: {
				struct user_regs_struct regs;
				ptrace(PTRACE_GETREGS, debug.pid, NULL, &regs);
				printf("rax 0x%lx\n", (uint64_t)regs.rax);
				printf("rbx 0x%lx\n", (uint64_t)regs.rbx);
				printf("rcx 0x%lx\n", (uint64_t)regs.rcx);
				printf("rdx 0x%lx\n", (uint64_t)regs.rdx);
				printf("rsi 0x%lx\n", (uint64_t)regs.rsi);
				printf("rdi 0x%lx\n", (uint64_t)regs.rdi);
				printf("rbp 0x%lx\n", (uint64_t)regs.rbp);
				printf("rsp 0x%lx\n", (uint64_t)regs.rsp);
				printf("rip 0x%lx\n", (uint64_t)regs.rip);
			} break;

			case LINES: {
				for (uint32_t u = 0; u < debug.info.numCompUnits; u++) {
					TcdCompUnit *cu = debug.info.compUnits + u;
					printf("%s/%s:\n", cu->compDir, cu->name);
					for (uint32_t i = 0; i < cu->numFuncs; i++) {
						TcdFunction *func = cu->funcs + i;
						printf("  %s:\n", func->name);
						for (uint32_t j = 0; j < func->numLines; j++) {
							printf("    %d:0x%lx\n", func->lines[j].number, func->lines[j].address);
						}
					}
				}
			} break;

			case TYPES: {
				for (uint32_t u = 0; u < debug.info.numCompUnits; u++) {
					TcdCompUnit *cu = debug.info.compUnits + u;
					printf("%s/%s:\n", cu->compDir, cu->name);
					for (uint32_t i = 0; i < cu->numTypes; i++) {
						TcdType *type = cu->types + i;
						switch (type->tclass)
						{
							case TCDT_BASE:
								printf("  %s: size=%d inter=%d\n", type->as.base.name, type->size, type->as.base.interp);
								break;
							case TCDT_POINTER:
								printf("  <pointer>: to=%p\n", (void*)type->as.pointer.to);
								break;
							default: break;
						}
					}
				}
			} break;

			case LOCALS: {
				uint64_t rip = tcdReadIP(&debug);
				TcdFunction *func = tcdSurroundingFunction(&debug.info, rip);
				if (func == NULL) break;
				for (uint32_t i = 0; i < func->numLocals; i++) {
					TcdLocal *local = func->locals + i;
					char type[1024] = {0};
					typeToString(local->type, type);
					TcdRtLoc rtloc = {0};
					if (tcdInterpretLocation(&debug, local->locdesc, &rtloc) != 0)
						continue;
					printf("%s (%s) 0x%lx\n", local->name, type, rtloc.address);
				}
			} break;

			case POINTS: {
				for (uint32_t i = 0; i < debug.numBreaks; i++) {
					printf("%d:0x%lx(line %d)\n", i, debug.breaks[i].address, debug.breaks[i].line);
				}
			} break;

			/* Dump 8 bytes of data at <arg> in hex */
			case DUMP: {
				uint64_t address;
				sscanf(arg1, "%lx", &address); /* TODO Check return value? */
				uint8_t bytes[4 * 8];
				tcdReadMemory(&debug, address, 4 * 8, bytes);
				for (uint32_t i = 0; i < 4 * 8; ) {
					printf("%02X ", bytes[i]);
					if (++i % 8 == 0) {
						printf("\n");
					}
				}
			} break;

			case PRINT: {
				TcdType *type;
				TcdRtLoc rtloc;
				if (cexprParse(&debug, arg1, &type, &rtloc) != 0) {
					printf("(input error)\n");
					break;
				}
				switch (type->tclass) {
					case TCDT_BASE: {
						switch (type->as.base.interp) {
							case TCDI_ADDRESS: break;
							case TCDI_SIGNED: {
								int64_t data;
								tcdReadRtLoc(&debug, rtloc, type->size, &data);
								printf("%ld\n", data);
							} break;
							case TCDI_UNSIGNED: {
								uint64_t data;
								tcdReadRtLoc(&debug, rtloc, type->size, &data);
								printf("%lu\n", data);
							} break;
							case TCDI_CHAR: { /* TODO wide characters */
								char data;
								tcdReadRtLoc(&debug, rtloc, 1, &data);
								printf("%d = '%c'\n", data, data);
							} break;
							case TCDI_UCHAR: { /* TODO wide characters */
								unsigned char data;
								tcdReadRtLoc(&debug, rtloc, 1, &data);
								printf("%u = '%c'\n", data, data);
							} break;
							case TCDI_FLOAT:
								if (type->size == 4) {
									float data;
									tcdReadRtLoc(&debug, rtloc, 4, &data);
									printf("%f\n", data);
								} else if (type->size == 8) {
									double data;
									tcdReadRtLoc(&debug, rtloc, 8, &data);
									printf("%lf\n", data);
								} else {
									printf("(error: floating point value is neither float nor double)\n");
								}
								break;
							case TCDI_BOOL: {
								uint8_t data;
								tcdReadRtLoc(&debug, rtloc, 1, &data);
								printf("%s\n", data ? "true" : "false");
							} break;
						}
					} break;
					case TCDT_POINTER: {
						uint64_t address;
						tcdReadRtLoc(&debug, rtloc, 8, &address);
						printf("(pointer) 0x%lx", address);
						if  (type->as.pointer.to->tclass == TCDT_BASE &&
							(type->as.pointer.to->as.base.interp == TCDI_CHAR ||
							 type->as.pointer.to->as.base.interp == TCDI_UCHAR)) {
							char data[256];
							data[255] = '\0';
							tcdReadRtLoc(&debug, (TcdRtLoc){address, TCDR_ADDRESS}, 255, data);
							printf(" = \"%s\"", data);
						}
						printf("\n");
					} break;
					case TCDT_ARRAY: {
						printf("(array)");
						if  (type->as.array.of->tclass == TCDT_BASE &&
							(type->as.array.of->as.base.interp == TCDI_CHAR ||
							 type->as.array.of->as.base.interp == TCDI_UCHAR)) {
							char data[256];
							data[255] = '\0';
							tcdReadRtLoc(&debug, rtloc, 255, data);
							printf(" = \"%s\"", data);
						}
						printf("\n");
					} break;
					default: break;
				}
				cexprFreeType(type);
			} break;

			/* Continue execution */
			case CONTINUE:
				ptrace(PTRACE_CONT, debug.pid, NULL, NULL);
				tcdSync(&debug);
				break;

			/* Kill process */
			case KILL:
				kill(debug.pid, SIGKILL);
				tcdSync(&debug);
				break;

			/* Error */
			default:
				fprintf(stderr, "invalid or unknown command\n");
				break;
		}
	}
	tcdFreeContext(&debug);
	return 0;
}
