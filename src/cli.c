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
	REGISTERS, LINES, LOCALS, POINTS,
	DUMP,
	INVALID
} Command;

/* Reads command from user */
static void getNextCommand(Command *cmd, char **argv) {
	char *cmdstr = readline(prompt);
	if (cmdstr == NULL) return;
	else if (cmdstr[0] == '\0') {
	} else if (strcmp(cmdstr, "continue") == 0) {
		*cmd = CONTINUE;
	} else if (strcmp(cmdstr, "kill") == 0) {
		*cmd = KILL;
	} else if (strcmp(cmdstr, "step") == 0) {
		*cmd = STEP;
	} else if (strcmp(cmdstr, "next") == 0) {
		*cmd = NEXT;
	} else if (strcmp(cmdstr, "trace") == 0) {
		*cmd = TRACE;
	} else if (strcmp(cmdstr, "where") == 0) {
		*cmd = WHERE;
	} else if (strcmp(cmdstr, "registers") == 0) {
		*cmd = REGISTERS;
	} else if (strcmp(cmdstr, "lines") == 0) {
		*cmd = LINES;
	} else if (strcmp(cmdstr, "locals") == 0) {
		*cmd = LOCALS;
	} else if (strcmp(cmdstr, "points") == 0) {
		*cmd = POINTS;
	} else {
		const char *op = strtok(cmdstr, " ");
		const char *arg = strtok(NULL, " ");
		if (!op || !arg) {
			*cmd = INVALID;
		} else {
			*argv = strdup(arg);
			if (strcmp(op, "break") == 0) {
				*cmd = BREAK;
			} else if (strcmp(op, "dump") == 0) {
				*cmd = DUMP;
			} else {
				*cmd = INVALID;
			}
		}
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
	char *arg;

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

		getNextCommand(&cmd, &arg);
		switch (cmd) {
			/* Set break point */
			case BREAK: {
				/* Find the address of the line */
				uint64_t address = 0;
				uint32_t line = 0;
				char symbol[256];
#if 0
				uint32_t number;
				char *colon = strchr(arg, ':');
				if (colon != NULL) {
					function_t *func = function_by_name(&data, symbol);
					if (func != NULL) {
						uint32_t offset = func->lines[0].number;
						line_t *mline = nearest_line(func, number + offset);
						address = mline->address;
						line = mline->number;
					} else {
						printf("Couldn't find function '%s'.\n", symbol);
					}
				} else if (sscanf(arg, "%d", &number) == 1) {
					for (uint32_t i = 0; i < data.numFuncs; i++) {
						uint32_t beg = data.funcs[i].lines[0].number;
						uint32_t end = data.funcs[i].lines[data.funcs[i].numLines - 1].number;
						if (number >= beg && number <= end) {
							for (uint32_t i = 0; i < data.funcs[i].numLines; i++) {
								if (number == data.funcs[i].lines[i].number) {
									address = data.funcs[i].lines[i].address;
									line = number;
								}
							}
							break;
						}
					}
					if (address == 0) {
						printf("Couldn't find line '%d'.\n", number);
					}
				} else
#endif
				if (sscanf(arg, "%s", symbol) == 1) {
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
					/* Insert new break point */
					TcdBreakpoint point;
					point.address = address;
					point.line = line;
					/* Save instruction at eip */
					tcdReadMemory(&debug, address, 1, &point.saved);
					/* Insert break point */
					uint8_t int3 = 0xCC;
					tcdWriteMemory(&debug, address, 1, &int3);
					/* Store break point */
					debug.breaks = realloc(debug.breaks, ++debug.numBreaks * sizeof(*debug.breaks));
					debug.breaks[debug.numBreaks - 1] = point;
					printf("Set breakpoint at ");
					printWhere(&debug.info, address);
				} else {
					printf("line %d not found.\n", line);
				}
				free(arg);
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

			case LOCALS: {
				struct user_regs_struct regs;
				ptrace(PTRACE_GETREGS, debug.pid, NULL, &regs);
				uint64_t rip = regs.rip;
				TcdFunction *func = tcdSurroundingFunction(&debug.info, rip);
				if (func == NULL) break;
				for (uint32_t i = 0; i < func->numLocals; i++) {
					TcdLocal *local = func->locals + i;
					TcdLocDesc desc = {local->exprloc, 0};
					TcdRtLoc rtloc = {0};
					if (tcdInterpretLocation(&debug, desc, &rtloc) != 0)
						continue;
					printf("%s 0x%lx\n", local->name, rtloc.address);
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
				sscanf(arg, "%lx", &address); /* TODO Check return value? */
				uint8_t bytes[4 * 8];
				tcdReadMemory(&debug, address, 4 * 8, bytes);
				for (uint32_t i = 0; i < 4 * 8; ) {
					printf("%02X ", bytes[i]);
					if (++i % 8 == 0) {
						printf("\n");
					}
				}
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
