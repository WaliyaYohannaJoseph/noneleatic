/* nevm.c virtual machine for the noneleatic languages */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include "arg.h"

typedef struct {
	char op;
	char dst_type;
	char src1_type;
	char src2_type;
	union {
		uint32_t u;
		int32_t i;
		float f;
	} dst;
	union {
		uint32_t u;
		int32_t i;
		float f;
	} src1;
	union {
		uint32_t u;
		int32_t i;
		float f;
	} src2;
} operation;

static struct {
	uint32_t brk_max;
} config = { 4 * 1024 * 1024 };

static struct {
	char *mem;
	uint32_t brk;
} machine = { NULL, 0 };

#define fatal(...) do {							\
	fprintf(stderr, __VA_ARGS__);					\
	exit(EXIT_FAILURE);						\
} while (0)

static int check_brk(uint32_t addr) {
	if (addr > machine.brk) {
		void *newmem;
		if (addr > config.brk_max) {
			errno = ENOMEM;
			return -1;
		}
		newmem = realloc(machine.mem, addr);
		if (newmem == NULL) {
			return -1;
		}
		machine.mem = newmem;
		machine.brk = addr;
	}
	return 0;
}

static void assert_brk(uint32_t addr, uint32_t addr_addr) {
	if (check_brk(addr) != 0) {
		fatal("Could not create memory for address at %u: %u\n",
		      addr_addr, addr);
	}
}

static void validate_arg(uint32_t addr, char arg_type,
			 uint32_t addr_addr, uint32_t arg_type_addr) {
	switch (arg_type) {
	case 'U':
	case 'I':
	case 'F':
		break;
	case 'z':
	case 'l':
	case 'd':
		assert_brk(addr + 8, addr_addr);
		break;
	case 'u':
	case 'i':
	case 'f':
		assert_brk(addr + 4, addr_addr);
		break;
	case 'h':
	case 's':
		assert_brk(addr + 2, addr_addr);
		break;
	case 'c':
	case 'b':
		assert_brk(addr + 1, addr_addr);
		break;
	default:
		fatal("Invalid type at %u: %c\n",
		      arg_type_addr, arg_type);
	}
}

static void validate_op(char op, uint32_t op_addr) {
	switch (op) {
	case '_': /* No op */
	case '=': /* Assign */
	case '@': /* Block transfer */
	case '!': /* Not */
	case '&': /* And */
	case '|': /* Or */
	case '^': /* Xor */
	case '<': /* Shift left */
	case '>': /* Shift right */
	case '~': /* Negate */
	case '+': /* Add */
	case '-': /* Subtract */
	case '*': /* Multiply */
	case '/': /* Divide */
	case '%': /* Remainder */
	case '#': /* Halt */
		break;
	default:
		fatal("Invalid operation at %u: %c\n",
		      op_addr, op);
	}
}

#define caddr2addr(caddr)						\
	((uint32_t) (((char *) (caddr)) - machine.mem))

#define addr2caddr(addr)						\
	(machine.mem + (addr))

#define indirect(addr, ctype)						\
	(*((ctype *) addr2caddr(addr)))

#define valaddr(arg, type)						\
	(  (type) == 'U' ? caddr2addr(&(arg).u)				\
	 : (type) == 'I' ? caddr2addr(&(arg).u)				\
	 : (type) == 'F' ? caddr2addr(&(arg).u)				\
	 : (arg).u)

#define valsize(type)							\
	(  (type) == 'U' ? (uint32_t) 4					\
	 : (type) == 'I' ? (uint32_t) 4					\
	 : (type) == 'F' ? (uint32_t) 4					\
	 : (type) == 'z' ? (uint32_t) 8					\
	 : (type) == 'l' ? (uint32_t) 8					\
	 : (type) == 'd' ? (uint32_t) 8					\
	 : (type) == 'u' ? (uint32_t) 4					\
	 : (type) == 'i' ? (uint32_t) 4					\
	 : (type) == 'f' ? (uint32_t) 4					\
	 : (type) == 'h' ? (uint32_t) 2					\
	 : (type) == 's' ? (uint32_t) 2					\
	 : (type) == 'c' ? (uint32_t) 1					\
	 : (type) == 'b' ? (uint32_t) 1					\
	 : (uint32_t) 0)

#define val(arg, type, ctype)						\
	(  (type) == 'U' ? (ctype) (arg).u				\
	 : (type) == 'I' ? (ctype) (arg).i				\
	 : (type) == 'F' ? (ctype) (arg).f				\
	 : (type) == 'z' ? (ctype) indirect((arg).u, uint64_t)		\
	 : (type) == 'l' ? (ctype) indirect((arg).u, int64_t)		\
	 : (type) == 'd' ? (ctype) indirect((arg).u, double)		\
	 : (type) == 'u' ? (ctype) indirect((arg).u, uint32_t)		\
	 : (type) == 'i' ? (ctype) indirect((arg).u, int32_t)		\
	 : (type) == 'f' ? (ctype) indirect((arg).u, float)		\
	 : (type) == 'h' ? (ctype) indirect((arg).u, uint16_t)		\
	 : (type) == 's' ? (ctype) indirect((arg).u, int16_t)		\
	 : (type) == 'c' ? (ctype) indirect((arg).u, uint8_t)		\
	 : (type) == 'b' ? (ctype) indirect((arg).u, int8_t)		\
	 : (ctype) 0)

#define unary_op(op, cop)						\
do {									\
	switch (op->dst_type) {						\
	case 'U':							\
		op->dst.u = cop val(op->src1, op->src1_type, uint32_t); \
		break;							\
	case 'I':							\
		op->dst.i = cop val(op->src1, op->src1_type, int32_t);	\
		break;							\
	case 'F':							\
		op->dst.f = cop val(op->src1, op->src1_type, float);	\
		break;							\
	case 'z':							\
		indirect(op->dst.u, uint64_t)				\
			= cop val(op->src1, op->src1_type, uint64_t);	\
		break;							\
	case 'l':							\
		indirect(op->dst.u, int64_t)				\
			= cop val(op->src1, op->src1_type, int64_t);	\
		break;							\
	case 'd':							\
		indirect(op->dst.u, double)				\
			= cop val(op->src1, op->src1_type, double);	\
		break;							\
	case 'u':							\
		indirect(op->dst.u, uint32_t)				\
			= cop val(op->src1, op->src1_type, uint32_t);	\
		break;							\
	case 'i':							\
		indirect(op->dst.u, int32_t)				\
			= cop val(op->src1, op->src1_type, int32_t);	\
		break;							\
	case 'f':							\
		indirect(op->dst.u, float)				\
			= cop val(op->src1, op->src1_type, float);	\
		break;							\
	case 'h':							\
		indirect(op->dst.u, uint16_t)				\
			= cop val(op->src1, op->src1_type, uint16_t);	\
		break;							\
	case 's':							\
		indirect(op->dst.u, int16_t)				\
			= cop val(op->src1, op->src1_type, int16_t);	\
		break;							\
	case 'c':							\
		indirect(op->dst.u, uint8_t)				\
			= cop val(op->src1, op->src1_type, uint8_t);	\
		break;							\
	case 'b':							\
		indirect(op->dst.u, int8_t)				\
			= cop val(op->src1, op->src1_type, int8_t);	\
		break;							\
	}								\
} while (0)

#define unary_op_nofloat(op, cop)					\
do {									\
	switch (op->dst_type) {						\
	case 'U':							\
		op->dst.u = cop val(op->src1, op->src1_type, uint32_t); \
		break;							\
	case 'I':							\
		op->dst.i = cop val(op->src1, op->src1_type, int32_t);	\
		break;							\
	case 'z':							\
		indirect(op->dst.u, uint64_t)				\
			= cop val(op->src1, op->src1_type, uint64_t);	\
		break;							\
	case 'l':							\
		indirect(op->dst.u, int64_t)				\
			= cop val(op->src1, op->src1_type, int64_t);	\
		break;							\
	case 'u':							\
		indirect(op->dst.u, uint32_t)				\
			= cop val(op->src1, op->src1_type, uint32_t);	\
		break;							\
	case 'i':							\
		indirect(op->dst.u, int32_t)				\
			= cop val(op->src1, op->src1_type, int32_t);	\
		break;							\
	case 'h':							\
		indirect(op->dst.u, uint16_t)				\
			= cop val(op->src1, op->src1_type, uint16_t);	\
		break;							\
	case 's':							\
		indirect(op->dst.u, int16_t)				\
			= cop val(op->src1, op->src1_type, int16_t);	\
		break;							\
	case 'c':							\
		indirect(op->dst.u, uint8_t)				\
			= cop val(op->src1, op->src1_type, uint8_t);	\
		break;							\
	case 'b':							\
		indirect(op->dst.u, int8_t)				\
			= cop val(op->src1, op->src1_type, int8_t);	\
		break;							\
	}								\
} while (0)

#define binary_op(op, cop)						\
do {									\
	switch (op->dst_type) {						\
	case 'U':							\
		op->dst.u = val(op->src1, op->src1_type, uint32_t)	\
			    cop val(op->src2, op->src2_type, uint32_t); \
		break;							\
	case 'I':							\
		op->dst.i = val(op->src1, op->src1_type, int32_t)	\
			    cop val(op->src2, op->src2_type, int32_t);	\
		break;							\
	case 'F':							\
		op->dst.f = val(op->src1, op->src1_type, float)	\
			    cop val(op->src2, op->src2_type, float);	\
		break;							\
	case 'z':							\
		indirect(op->dst.u, uint64_t)				\
			= val(op->src1, op->src1_type, uint64_t)	\
			  cop val(op->src2, op->src2_type, uint64_t);	\
		break;							\
	case 'l':							\
		indirect(op->dst.u, int64_t)				\
			= val(op->src1, op->src1_type, int64_t)	\
			  cop val(op->src2, op->src2_type, int64_t);	\
		break;							\
	case 'd':							\
		indirect(op->dst.u, double)				\
			= val(op->src1, op->src1_type, double)		\
			  cop val(op->src2, op->src2_type, double);	\
		break;							\
	case 'u':							\
		indirect(op->dst.u, uint32_t)				\
			= val(op->src1, op->src1_type, uint32_t)	\
			  cop val(op->src2, op->src2_type, uint32_t);	\
		break;							\
	case 'i':							\
		indirect(op->dst.u, int32_t)				\
			= val(op->src1, op->src1_type, int32_t)	\
			  cop val(op->src2, op->src2_type, int32_t);	\
		break;							\
	case 'f':							\
		indirect(op->dst.u, float)				\
			= val(op->src1, op->src1_type, float)		\
			  cop val(op->src2, op->src2_type, float);	\
		break;							\
	case 'h':							\
		indirect(op->dst.u, uint16_t)				\
			= val(op->src1, op->src1_type, uint16_t)	\
			  cop val(op->src2, op->src2_type, uint16_t);	\
		break;							\
	case 's':							\
		indirect(op->dst.u, int16_t)				\
			= val(op->src1, op->src1_type, int16_t)	\
			  cop val(op->src2, op->src2_type, int16_t);	\
		break;							\
	case 'c':							\
		indirect(op->dst.u, uint8_t)				\
			= val(op->src1, op->src1_type, uint8_t)	\
			  cop val(op->src2, op->src2_type, uint8_t);	\
		break;							\
	case 'b':							\
		indirect(op->dst.u, int8_t)				\
			= val(op->src1, op->src1_type, int8_t)		\
			  cop val(op->src2, op->src2_type, int8_t);	\
		break;							\
	}								\
} while (0)

#define binary_op_nofloat(op, cop)					\
do {									\
	switch (op->dst_type) {						\
	case 'U':							\
		op->dst.u = val(op->src1, op->src1_type, uint32_t)	\
			    cop val(op->src2, op->src2_type, uint32_t); \
		break;							\
	case 'I':							\
		op->dst.i = val(op->src1, op->src1_type, int32_t)	\
			    cop val(op->src2, op->src2_type, int32_t);	\
		break;							\
	case 'z':							\
		indirect(op->dst.u, uint64_t)				\
			= val(op->src1, op->src1_type, uint64_t)	\
			  cop val(op->src2, op->src2_type, uint64_t);	\
		break;							\
	case 'l':							\
		indirect(op->dst.u, int64_t)				\
			= val(op->src1, op->src1_type, int64_t)	\
			  cop val(op->src2, op->src2_type, int64_t);	\
		break;							\
	case 'u':							\
		indirect(op->dst.u, uint32_t)				\
			= val(op->src1, op->src1_type, uint32_t)	\
			  cop val(op->src2, op->src2_type, uint32_t);	\
		break;							\
	case 'i':							\
		indirect(op->dst.u, int32_t)				\
			= val(op->src1, op->src1_type, int32_t)	\
			  cop val(op->src2, op->src2_type, int32_t);	\
		break;							\
	case 'h':							\
		indirect(op->dst.u, uint16_t)				\
			= val(op->src1, op->src1_type, uint16_t)	\
			  cop val(op->src2, op->src2_type, uint16_t);	\
		break;							\
	case 's':							\
		indirect(op->dst.u, int16_t)				\
			= val(op->src1, op->src1_type, int16_t)	\
			  cop val(op->src2, op->src2_type, int16_t);	\
		break;							\
	case 'c':							\
		indirect(op->dst.u, uint8_t)				\
			= val(op->src1, op->src1_type, uint8_t)	\
			  cop val(op->src2, op->src2_type, uint8_t);	\
		break;							\
	case 'b':							\
		indirect(op->dst.u, int8_t)				\
			= val(op->src1, op->src1_type, int8_t)		\
			  cop val(op->src2, op->src2_type, int8_t);	\
		break;							\
	}								\
} while (0)

static void print_mem() {
	uint32_t i;
	for (i = 0; i < machine.brk; i++) {
		printf("%c", indirect(i, char));
	}
}

static void run() {
	operation *op;
	uint32_t ip;
	int r;
	for (;;) {
		print_mem();
		ip = indirect(0, uint32_t);
		r = check_brk(ip + sizeof(operation));
		if (r != 0) {
			fatal("Invalid IP: %u\n", ip);
		}
		op = (operation *) (machine.mem + ip);
		validate_op(op->op, caddr2addr(&op->op));
		validate_arg(op->dst.u, op->dst_type,
			     caddr2addr(&op->dst.u),
			     caddr2addr(&op->dst_type));
		validate_arg(op->src1.u, op->src1_type,
			     caddr2addr(&op->src1.u),
			     caddr2addr(&op->src1_type));
		validate_arg(op->src2.u, op->src2_type,
			     caddr2addr(&op->src2.u),
			     caddr2addr(&op->src2_type));
		/* op-specific validation */
		switch (op->op) {
		case '@':
			assert_brk(valaddr(op->dst, op->dst_type)
				   + valsize(op->dst_type)
				     * val(op->src2, op->src2_type, uint32_t),
				   caddr2addr(&op->dst.u));
			assert_brk(valaddr(op->src1, op->src1_type)
				   + valsize(op->dst_type)
				     * val(op->src2, op->src2_type, uint32_t),
				   caddr2addr(&op->src1.u));
			break;
		case '!':
		case '&':
		case '|':
		case '^':
		case '<':
		case '>':
			if (op->dst_type == 'F'
			    || op->dst_type == 'f'
			    || op->dst_type == 'd') {
				fatal("Invalid type at %u: %c. Floating"
				      " type cannot be used with bitwise"
				      " operator %c\n",
				      caddr2addr(&op->dst_type), op->dst_type, op->op);
			}
			break;
		}
		indirect(0, uint32_t) = ip + sizeof(operation);
		switch (op->op) {
		case '_':
			/* No-op */
			break;
		case '=':
			/* Assign */
			unary_op(op, +);
			break;
		case '@':
			memmove(addr2caddr(valaddr(op->dst, op->dst_type)),
				addr2caddr(valaddr(op->src1, op->src1_type)),
				valsize(op->dst_type) * val(op->src2, op->src2_type, uint32_t));
			break;
		case '!':
			unary_op_nofloat(op, ~);
			break;
		case '&':
			binary_op_nofloat(op, &);
			break;
		case '|':
			binary_op_nofloat(op, |);
			break;
		case '^':
			binary_op_nofloat(op, ^);
			break;
		case '<':
			binary_op_nofloat(op, <<);
			break;
		case '>':
			binary_op_nofloat(op, >>);
			break;
		case '~':
			unary_op(op, -);
			break;
		case '+':
			binary_op(op, +);
			break;
		case '-':
			binary_op(op, -);
			break;
		case '*':
			binary_op(op, *);
			break;
		case '/':
			binary_op(op, /);
			break;
		case '%':
			switch (op->dst_type) {
			case 'F':
				op->dst.f = fmodf(val(op->src1, op->src1_type, float),
						  val(op->src2, op->src2_type, float));
				break;
			case 'f':
				indirect(op->dst.u, float)
					= fmodf(val(op->src1, op->src1_type, float),
						val(op->src2, op->src2_type, float));
				break;
			case 'd':
				indirect(op->dst.u, double)
					= fmod(val(op->src1, op->src1_type, double),
					       val(op->src2, op->src2_type, double));
				break;
			default:
				binary_op_nofloat(op, %);
				break;
			}
			break;
		case '#':
			return;
		}
	}
}

static void usage() {
	fatal("%s [-l location] file [[-l location] file] ...\n", argv0);
}

#define CHUNK_SIZE 4096

static void load_file(uint32_t *mem_cursor, char *filename) {
	FILE *f;
	size_t r;
	fprintf(stderr, "Loading %s at %u\n", filename, *mem_cursor);
	f = fopen(filename, "r");
	if (f == NULL) {
		fatal("Couldn't open file \"%s\": %s", filename, strerror(errno));
	}
	for (;;) {
		if (check_brk(*mem_cursor + CHUNK_SIZE) != 0) {
			fatal("Could not create memory for file \"%s\" at %u",
			      filename, *mem_cursor + CHUNK_SIZE);
		}
		r = fread(addr2caddr(*mem_cursor), 1, CHUNK_SIZE, f);
		*mem_cursor += r;
		if (r != CHUNK_SIZE) {
			if (feof(f)) {
				fclose(f);
				return;
			} else if (ferror(f)) {
				fatal("Couldn't read from file \"%s\": %s",
				      filename, strerror(errno));
			}
		}
	}
}

int main(int argc, char **argv) {
	uint32_t mem_cursor = 0;
	argv0 = argv[0];
	argv++, argc--;
	for (; argc > 0; argv++, argc--) {
		if (argv[0][0] == '-') {
			switch (argv[0][1]) {
			case '-':
				/* No more arguments */
				argv++, argc--;
				goto fileonly;
			case 'l':
				if (argv[0][2] != '\0') {
					mem_cursor = atoi(argv[0]+2);
				} else {
					argv++, argc--;
					if (argc == 0) {
						usage();
					}
					mem_cursor = atoi(argv[0]);
				}
				break;
			default:
				usage();
			}
		} else {
			load_file(&mem_cursor, argv[0]);
		}
	}
	goto run;
fileonly:
	for (; argc > 0; argv++, argc--) {
		load_file(&mem_cursor, argv[0]);
	}
run:
	run();
	exit(EXIT_SUCCESS);
}
