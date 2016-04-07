#include "tcd.h"

#include <stdlib.h>
#include <stdbool.h>
#include <libdwarf/dwarf.h>

uint64_t decodeUnsignedLeb128(uint8_t **data) {
	uint64_t result = 0;
	uint8_t shift = 0;
	uint8_t byte;
	do {
		byte = **data;
		(*data)++;
		result |= (byte | 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);
	return result;
}

int64_t decodeSignedLeb128(uint8_t **data) {
	int64_t result = 0;
	uint8_t shift = 0;
	uint8_t byte;
	do {
		byte = **data;
		(*data)++;
		result |= (byte & 0x7F) << shift;
		shift += 7;
	} while (byte & 0x80);
	// Sign extend negative numbers.
	if (byte & 0x40)
		result |= (-1ULL) << shift;
	return result;
}

/** Logical right shift **/
int64_t shrLog(int64_t x, int64_t n) {
    return (uint64_t)x >> n;
}

/** Arithmetical right shift **/
int64_t shrAr(int64_t x, int64_t n) {
    if (x < 0 && n > 0)
        return x >> n | ~(~0U >> n);
    else
        return x >> n;
}

struct StackElem {
	int64_t data;
	struct StackElem *next;
};

static void stackPush(struct StackElem **stack, int64_t value) {
	struct StackElem *elem = malloc(sizeof(**stack));
	elem->data = value;
	elem->next = *stack;
	*stack = elem;
}

static int64_t stackPop(struct StackElem **stack) {
	struct StackElem *old = *stack;
	int64_t ret = old->data;
	*stack = (*stack)->next;
	free(old);
	return ret;
}

#define ADDR_BINOP(n, o) \
case n: \
	first  = stackPop(&stack); \
	second = stackPop(&stack); \
	stackPush(&stack, second o first); \
	break;
#define ADDR_COND(n, o) \
case n: \
	first  = stackPop(&stack); \
	second = stackPop(&stack); \
	stackPush(&stack, (second o first) ? 1 : 0); \
	break;

int tcdInterpretLocation(TcdContext *debug, TcdLocDesc desc, TcdRtLoc *rtloc) {
	int res = 0;
	bool done = false;
	uint8_t *instr = desc.expr;
	struct StackElem *stack = NULL;
	while (!done) {
		uint8_t op = *instr++;
		int64_t first, second;
		switch (op) {
			case DW_OP_nop: break;
			case DW_OP_lit0:  case DW_OP_lit1:  case DW_OP_lit2:  case DW_OP_lit3:
			case DW_OP_lit4:  case DW_OP_lit5:  case DW_OP_lit6:  case DW_OP_lit7:
			case DW_OP_lit8:  case DW_OP_lit9:  case DW_OP_lit10: case DW_OP_lit11:
			case DW_OP_lit12: case DW_OP_lit13: case DW_OP_lit14: case DW_OP_lit15:
			case DW_OP_lit16: case DW_OP_lit17: case DW_OP_lit18: case DW_OP_lit19:
			case DW_OP_lit20: case DW_OP_lit21: case DW_OP_lit22: case DW_OP_lit23:
			case DW_OP_lit24: case DW_OP_lit25: case DW_OP_lit26: case DW_OP_lit27:
			case DW_OP_lit28: case DW_OP_lit29: case DW_OP_lit30: case DW_OP_lit31:
				stackPush(&stack, op - DW_OP_lit0);
				break;
#if 0
			case DW_OP_reg0:  case DW_OP_reg1:  case DW_OP_reg2:  case DW_OP_reg3:
			case DW_OP_reg4:  case DW_OP_reg5:  case DW_OP_reg6:  case DW_OP_reg7:
			case DW_OP_reg8:  case DW_OP_reg9:  case DW_OP_reg10: case DW_OP_reg11:
			case DW_OP_reg12: case DW_OP_reg13: case DW_OP_reg14: case DW_OP_reg15:
			case DW_OP_reg16: case DW_OP_reg17: case DW_OP_reg18: case DW_OP_reg19:
			case DW_OP_reg20: case DW_OP_reg21: case DW_OP_reg22: case DW_OP_reg23:
			case DW_OP_reg24: case DW_OP_reg25: case DW_OP_reg26: case DW_OP_reg27:
			case DW_OP_reg28: case DW_OP_reg29: case DW_OP_reg30: case DW_OP_reg31:
				rtloc->address = op - DW_OP_reg0;
				if (rtloc->address >= input.numRegs) {
					res = -1;
				}
				rtloc->region = TCDR_REGISTER;
				done = true;
				break;
			case DW_OP_breg0:  case DW_OP_breg1:  case DW_OP_breg2:  case DW_OP_breg3:
			case DW_OP_breg4:  case DW_OP_breg5:  case DW_OP_breg6:  case DW_OP_breg7:
			case DW_OP_breg8:  case DW_OP_breg9:  case DW_OP_breg10: case DW_OP_breg11:
			case DW_OP_breg12: case DW_OP_breg13: case DW_OP_breg14: case DW_OP_breg15:
			case DW_OP_breg16: case DW_OP_breg17: case DW_OP_breg18: case DW_OP_breg19:
			case DW_OP_breg20: case DW_OP_breg21: case DW_OP_breg22: case DW_OP_breg23:
			case DW_OP_breg24: case DW_OP_breg25: case DW_OP_breg26: case DW_OP_breg27:
			case DW_OP_breg28: case DW_OP_breg29: case DW_OP_breg30: case DW_OP_breg31: {
				uint32_t reg = op - DW_OP_breg0;
				if (reg >= input.numRegs) {
					res = -1;
					done = true;
					break;
				}
				int64_t address = input.regs[op - DW_OP_breg0];
				address += decodeSignedLeb128(&instr);
				stackPush(&stack, address);
			} break;
#endif
			case DW_OP_fbreg: {
				int64_t address = tcdReadBP(debug);
				address += decodeSignedLeb128(&instr);
				stackPush(&stack, address);
			} break;
			case DW_OP_const1u: case DW_OP_const1s:
				stackPush(&stack, *instr);
				instr++;
				break;
			case DW_OP_const2u: case DW_OP_const2s:
				stackPush(&stack, *(int16_t*)instr);
				instr += 2;
				break;
			case DW_OP_const4u: case DW_OP_const4s:
				stackPush(&stack, *(int32_t*)instr);
				instr += 4;
				break;
			case DW_OP_const8u: case DW_OP_const8s:
				stackPush(&stack, *(int64_t*)instr);
				instr += 8;
				break;
			case DW_OP_constu:
				stackPush(&stack, decodeUnsignedLeb128(&instr));
				break;
			case DW_OP_consts:
				stackPush(&stack, decodeSignedLeb128(&instr));
				break;
			case DW_OP_push_object_address:
				stackPush(&stack, desc.baseAddress);
				break;
			case DW_OP_dup:
				stackPush(&stack, stack->data);
				break;
			case DW_OP_drop:
				stackPop(&stack);
				break;
			case DW_OP_over:
				stackPush(&stack, stack->next->data);
				break;
			case DW_OP_swap:
				first = stack->data;
				stack->data = stack->next->data;
				stack->next->data = first;
				break;
			case DW_OP_abs:
				stack->data = stack->data >= 0 ? stack->data : -stack->data;
				break;
			ADDR_BINOP(DW_OP_and,   &);
			ADDR_BINOP(DW_OP_or,    |);
			ADDR_BINOP(DW_OP_xor,   ^);
			ADDR_BINOP(DW_OP_plus,  +);
			ADDR_BINOP(DW_OP_minus, -);
			ADDR_BINOP(DW_OP_mul,   *);
			ADDR_BINOP(DW_OP_div,   /);
			ADDR_BINOP(DW_OP_mod,   %);
			ADDR_COND (DW_OP_le,   <=);
			ADDR_COND (DW_OP_ge,   >=);
			ADDR_COND (DW_OP_eq,   ==);
			ADDR_COND (DW_OP_ne,   !=);
			ADDR_COND (DW_OP_lt,    <);
			ADDR_COND (DW_OP_gt,    >);
			case DW_OP_neg:
				stack->data = -stack->data;
				break;
			case DW_OP_not:
				stack->data = ~stack->data;
				break;
			case DW_OP_plus_uconst: {
				uint64_t cv = decodeUnsignedLeb128(&instr);
				stack->data += cv;
			} break;
			case DW_OP_skip: {
				int16_t skip = *(int16_t*)instr;
				instr = instr + 2 + skip;
			} break;
			case DW_OP_bra: {
				int64_t flag = stackPop(&stack);
				if (flag) {
					instr += *(int16_t*)instr;
				}
				instr += 2;
			} break;
			default:
				res = -1;
				done = true;
				break;
		}
		if (*instr == 0) {
			rtloc->address = stack->data;
			rtloc->region = TCDR_ADDRESS;
			done = true;
		}
	}
	while (stack != NULL) {
		stackPop(&stack);
	}
	return res;
}

int tcdDeref(TcdContext *debug, TcdType in_type, TcdRtLoc in_rtloc, TcdType *out_type, TcdRtLoc *out_rtloc) {
	return -1;
}

int tcdDerefIndex(TcdContext *debug, TcdType in_type, TcdRtLoc in_rtloc, uint64_t index, TcdType *out_type, TcdRtLoc *out_rtloc) {
	return -1;
}
