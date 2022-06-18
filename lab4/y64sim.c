/* Instruction set simulator for Y64 Architecture */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "y64sim.h"

#define err_print(_s, _a...) fprintf(stdout, _s "\n", _a)

typedef enum { STAT_AOK, STAT_HLT, STAT_ADR, STAT_INS } stat_t;

char *stat_names[] = {"AOK", "HLT", "ADR", "INS"};

char *stat_name(stat_t e) {
  if (e < STAT_AOK || e > STAT_INS)
    return "Invalid Status";
  return stat_names[e];
}

char *cc_names[8] = {"Z=0 S=0 O=0", "Z=0 S=0 O=1", "Z=0 S=1 O=0",
                     "Z=0 S=1 O=1", "Z=1 S=0 O=0", "Z=1 S=0 O=1",
                     "Z=1 S=1 O=0", "Z=1 S=1 O=1"};

char *cc_name(cc_t c) {
  int ci = c;
  if (ci < 0 || ci > 7)
    return "???????????";
  else
    return cc_names[c];
}

bool_t get_byte_val(mem_t *m, long_t addr, byte_t *dest) {
  if (addr < 0 || addr >= m->len)
    return FALSE;
  *dest = m->data[addr];
  return TRUE;
}

bool_t get_long_val(mem_t *m, long_t addr, long_t *dest) {
  int i;
  long_t val;
  if (addr < 0 || addr + 8 > m->len)
    return FALSE;
  val = 0;
  for (i = 0; i < 8; i++)
    val = val | ((long_t)m->data[addr + i]) << (8 * i);
  *dest = val;
  return TRUE;
}

__attribute__((unused)) bool_t set_byte_val(mem_t *m, long_t addr, byte_t val) {
  if (addr < 0 || addr >= m->len)
    return FALSE;
  m->data[addr] = val;
  return TRUE;
}

bool_t set_long_val(mem_t *m, long_t addr, long_t val) {
  int i;
  if (addr < 0 || addr + 8 > m->len)
    return FALSE;
  for (i = 0; i < 8; i++) {
    m->data[addr + i] = val & 0xFF;
    val >>= 8;
  }
  return TRUE;
}

mem_t *init_mem(unsigned long len) {
  mem_t *m = (mem_t *)malloc(sizeof(mem_t));
  len = ((len + BLK_SIZE - 1) / BLK_SIZE) * BLK_SIZE;
  m->len = len;
  m->data = (byte_t *)calloc(len, 1);

  return m;
}

void free_mem(mem_t *m) {
  free((void *)m->data);
  free((void *)m);
}

mem_t *dup_mem(mem_t *old_mem) {
  mem_t *newm = init_mem(old_mem->len);
  memcpy(newm->data, old_mem->data, old_mem->len);
  return newm;
}

bool_t diff_mem(mem_t *old_mem, mem_t *new_mem, FILE *outfile) {
  long_t pos;
  unsigned long len = old_mem->len;
  bool_t diff = FALSE;

  if (new_mem->len < len)
    len = new_mem->len;

  for (pos = 0; (!diff || outfile) && pos < len; pos += 8) {
    long_t ov = 0;
    long_t nv = 0;
    get_long_val(old_mem, pos, &ov);
    get_long_val(new_mem, pos, &nv);
    if (nv != ov) {
      diff = TRUE;
      if (outfile)
        fprintf(outfile, "0x%.16lx:\t0x%.16lx\t0x%.16lx\n", pos, ov, nv);
    }
  }
  return diff;
}

reg_t reg_table[REG_NONE] = {
    {"%rax", REG_RAX}, {"%rcx", REG_RCX}, {"%rdx", REG_RDX}, {"%rbx", REG_RBX},
    {"%rsp", REG_RSP}, {"%rbp", REG_RBP}, {"%rsi", REG_RSI}, {"%rdi", REG_RDI},
    {"%r8", REG_R8},   {"%r9", REG_R9},   {"%r10", REG_R10}, {"%r11", REG_R11},
    {"%r12", REG_R12}, {"%r13", REG_R13}, {"%r14", REG_R14}};

long_t get_reg_val(mem_t *r, regid_t id) {
  long_t val = 0;
  if (id >= REG_NONE)
    return 0;
  get_long_val(r, id * 8, &val);
  return val;
}

void set_reg_val(mem_t *r, regid_t id, long_t val) {
  if (id < REG_NONE)
    set_long_val(r, id * 8, val);
}

mem_t *init_reg() { return init_mem(REG_SIZE); }

void free_reg(mem_t *r) { free_mem(r); }

mem_t *dup_reg(mem_t *oldr) { return dup_mem(oldr); }

bool_t diff_reg(mem_t *oldr, mem_t *newr, FILE *outfile) {
  long_t pos;
  unsigned long len = oldr->len;
  bool_t diff = FALSE;

  if (newr->len < len)
    len = newr->len;

  for (pos = 0; (!diff || outfile) && pos < len; pos += 8) {
    long_t ov = 0;
    long_t nv = 0;
    get_long_val(oldr, pos, &ov);
    get_long_val(newr, pos, &nv);
    if (nv != ov) {
      diff = TRUE;
      if (outfile)
        fprintf(outfile, "%s:\t0x%.16lx\t0x%.16lx\n", reg_table[pos / 8].name,
                ov, nv);
    }
  }
  return diff;
}

/* create an y64 image with registers and memory */
y64sim_t *new_y64sim(int slen) {
  y64sim_t *sim = (y64sim_t *)malloc(sizeof(y64sim_t));
  sim->pc = 0;
  sim->r = init_reg();
  sim->m = init_mem(slen);
  sim->cc = DEFAULT_CC;
  return sim;
}

void free_y64sim(y64sim_t *sim) {
  free_reg(sim->r);
  free_mem(sim->m);
  free((void *)sim);
}

/* load binary code and data from file to memory image */
int load_binfile(mem_t *m, FILE *f) {
  unsigned long flen;

  clearerr(f);
  flen = fread(m->data, sizeof(byte_t), m->len, f);
  if (ferror(f)) {
    err_print("fread() failed (0x%lx)", flen);
    return -1;
  }
  if (!feof(f)) {
    err_print("too large memory footprint (0x%lx)", flen);
    return -1;
  }
  return 0;
}

/*
 * instruction_need_reg
 */
bool_t instruction_need_reg(itype_t icode) {
  switch (icode) {
  case I_HALT:
  case I_NOP:
  case I_RET:
  case I_JMP:
  case I_CALL:
    return FALSE;
  case I_RRMOVQ:
  case I_IRMOVQ:
  case I_RMMOVQ:
  case I_MRMOVQ:
  case I_ALU:
  case I_PUSHQ:
  case I_POPQ:
    return TRUE;
  case I_DIRECTIVE:
  default:
    // Should Abort
    return FALSE;
  }
}
/*
 * instruction_need_imm
 */
bool_t instruction_need_imm(itype_t icode) {
  switch (icode) {
  case I_HALT:
  case I_NOP:
  case I_RET:
  case I_ALU:
  case I_PUSHQ:
  case I_POPQ:
  case I_RRMOVQ:
    return FALSE;
  case I_IRMOVQ:
  case I_RMMOVQ:
  case I_MRMOVQ:
  case I_JMP:
  case I_CALL:
    return TRUE;
  case I_DIRECTIVE:
  default:
    // Should Abort
    return FALSE;
  }
}

/*
 * compute_alu: do ALU operations
 * args
 *     op: operations (A_ADD, A_SUB, A_AND, A_XOR)
 *     argA: the first argument
 *     argB: the second argument
 *
 * return
 *     the result of operation on argA and argB
 */
long_t compute_alu(alu_t op, long_t argA, long_t argB) {
  switch (op) {
  case A_ADD:
    return argA + argB;
  case A_SUB:
    return argB - argA;
  case A_AND:
    return argA & argB;
  case A_XOR:
    return argA ^ argB;
  default:
    // Should Abort
    return 0;
  }
}

/*
 * compute_cc: modify condition codes according to operations
 * args
 *     op: operations (A_ADD, A_SUB, A_AND, A_XOR)
 *     argA: the first argument
 *     argB: the second argument
 *     val: the result of operation on argA and argB
 *
 * return
 *     PACK_CC: the final condition codes
 */
cc_t compute_cc(alu_t op, long_t argA, long_t argB, long_t val) {
  bool_t zero = (val == 0);
  bool_t sign = (val < 0);
  bool_t ovf = FALSE;
  if ((op == A_ADD && (argA > 0) == (argB > 0) && (argA > 0) != (val > 0)) ||
      (op == A_SUB && (argB > 0) == (argA < 0) && (argB > 0) != (val > 0))) {
    ovf = TRUE;
  }

  return PACK_CC(zero, sign, ovf);
}

/*
 * cond_doit: whether do (mov or jmp) it?
 * args
 *     PACK_CC: the current condition codes
 *     cond: conditions (C_YES, C_LE, C_L, C_E, C_NE, C_GE, C_G)
 *
 * return
 *     TRUE: do it
 *     FALSE: not do it
 */
bool_t cond_doit(cc_t cc, cond_t cond) {
  cc_t SF = GET_SF(cc);
  cc_t OF = GET_OF(cc);
  cc_t ZF = GET_ZF(cc);

  switch (cond) {

  case C_YES:
    return TRUE;
  case C_LE:
    return (SF != OF) || ZF;
  case C_L:
    return SF != OF;
  case C_E:
    return ZF;
  case C_NE:
    return !ZF;
  case C_GE:
    return SF == OF;
  case C_G:
    return SF == OF && !ZF;
  default:
    // Should Abort
    return FALSE;
  }
}

/*
 * nexti: execute single instruction and return status.
 * args
 *     sim: the y64 image with PC, register and memory
 *
 * return
 *     STAT_AOK: continue
 *     STAT_HLT: halt
 *     STAT_ADR: invalid instruction address
 *     STAT_INS: invalid instruction, register id, data address, stack address,
 * ...
 */
stat_t nexti(y64sim_t *sim) {
  byte_t codefun = 0; /* 1 byte */
  itype_t icode;
  alu_t ifun;
  long_t next_pc = sim->pc;

  /* get code and function （1 byte) */
  if (!get_byte_val(sim->m, next_pc, &codefun)) {
    err_print("PC = 0x%lx, Invalid instruction address", sim->pc);
    return STAT_ADR;
  }
  icode = GET_ICODE(codefun);
  ifun = GET_FUN(codefun);
  next_pc++;

  /*check if instruction|function is valid */

  if (!check_code_fun_valid[codefun]) {
    err_print("PC = 0x%lx, Invalid instruction %.2x", sim->pc, codefun);
    return STAT_INS;
  }

  /* get registers if needed (1 byte) */
  byte_t regs = 0;
  regid_t reg_a = REG_NONE;
  regid_t reg_b = REG_NONE;
  long_t reg_a_val = 0;
  long_t reg_b_val = 0;
  long_t reg_s_val = get_reg_val(sim->r, REG_RSP);
  if (instruction_need_reg(icode)) {
    if (!get_byte_val(sim->m, next_pc, &regs)) {
      err_print("PC = 0x%lx, Invalid instruction address", sim->pc);
      return STAT_ADR;
    }
    reg_a = GET_REGA(regs);
    reg_a_val = get_reg_val(sim->r, reg_a);

    reg_b = GET_REGB(regs);
    reg_b_val = get_reg_val(sim->r, reg_b);

    next_pc++;
  }
  /* get immediate if needed (8 bytes) */
  long_t imm = 0;
  if (instruction_need_imm(icode)) {
    if (!get_long_val(sim->m, next_pc, &imm)) {
      err_print("PC = 0x%lx, Invalid instruction address", sim->pc);
      return STAT_ADR;
    }
    next_pc += 8;
  }

  long_t val;

  /* execute the instruction*/
  switch (icode) {
  case I_HALT: /* 0:0 */
    return STAT_HLT;
  case I_NOP: /* 1:0 */
    break;
  case I_RRMOVQ: /* 2:x regA:regB */
    if (cond_doit(sim->cc, (cond_t)ifun)) {
      set_reg_val(sim->r, reg_b, reg_a_val);
    }
    break;
  case I_IRMOVQ: /* 3:0 F:regB imm */
    set_reg_val(sim->r, reg_b, imm);
    break;
  case I_RMMOVQ: /* 4:0 regA:regB imm */
    if (set_long_val(sim->m, reg_b_val + imm, reg_a_val) == FALSE) {
      err_print("PC = 0x%lx, Invalid memory address 0x%lx", sim->pc,
                reg_b_val + imm);
      return STAT_ADR;
    }
    break;
  case I_MRMOVQ: /* 5:0 regB:regA imm */
    if (get_long_val(sim->m, reg_b_val + imm, &imm) == FALSE) {
      err_print("PC = 0x%lx, Invalid data address 0x%lx", sim->pc,
                reg_b_val + imm);
      return STAT_ADR;
    }
    set_reg_val(sim->r, reg_a, imm);
    break;
  case I_ALU: /* 6:x regA:regB */
    val = compute_alu(ifun, reg_a_val, reg_b_val);
    sim->cc = compute_cc(ifun, reg_a_val, reg_b_val, val);
    set_reg_val(sim->r, reg_b, val);
    break;
  case I_JMP: /* 7:x imm */
    if (cond_doit(sim->cc, (cond_t)ifun)) {
      next_pc = imm;
    }
    break;
  case I_CALL: /* 8:x imm */
    set_reg_val(sim->r, REG_RSP, reg_s_val - 8);
    if (set_long_val(sim->m, reg_s_val - 8, next_pc) == FALSE) {
      err_print("PC = 0x%lx, Invalid stack address 0x%lx", sim->pc,
                reg_s_val - 8);
      return STAT_ADR;
    }
    next_pc = imm;
    break;
  case I_RET: /* 9:0 */
    set_reg_val(sim->r, REG_RSP, reg_s_val + 8);
    if (get_long_val(sim->m, reg_s_val, &next_pc) == FALSE) {
      err_print("PC = 0x%lx, Invalid memory address 0x%lx", sim->pc, reg_s_val);
      return STAT_ADR;
    }
    break;
  case I_PUSHQ: /* A:0 regA:F */
    set_reg_val(sim->r, REG_RSP, reg_s_val - 8);
    if (set_long_val(sim->m, reg_s_val - 8, reg_a_val) == FALSE) {
      err_print("PC = 0x%lx, Invalid stack address 0x%lx", sim->pc,
                reg_s_val - 8);
      return STAT_ADR;
    }
    break;
  case I_POPQ: /* B:0 regA:F */
    set_reg_val(sim->r, REG_RSP, reg_s_val + 8);
    if (get_long_val(sim->m, reg_s_val, &imm) == FALSE) {
      err_print("PC = 0x%lx, Invalid stack address 0x%lx", sim->pc, reg_s_val);
      return STAT_ADR;
    }
    set_reg_val(sim->r, reg_a, imm);
    break;
  default:
    break;
  }
  sim->pc = next_pc;
  return STAT_AOK;
}

void usage(char *pname) {
  printf("Usage: %s file.bin [max_steps]\n", pname);
  exit(0);
}

int main(int argc, char *argv[]) {
  FILE *binfile;
  long long max_steps = MAX_STEP;
  y64sim_t *sim;
  mem_t *saver, *savem;
  int step;
  stat_t e = STAT_AOK;

  if (argc < 2 || argc > 3)
    usage(argv[0]);

  /* set max steps */
  errno = 0;
  if (argc > 2)
    max_steps = strtoll(argv[2], NULL, 10);
  if ((errno == ERANGE && (max_steps == LONG_MAX || max_steps == LONG_MIN)) ||
      (errno != 0 && max_steps == 0)) {
    err_print("Invalid step  '%s'", argv[2]);
    exit(EXIT_FAILURE);
  }
  /* load binary file to memory */
  if (strcmp(argv[1] + (strlen(argv[1]) - 4), ".bin") != 0)
    usage(argv[0]); /* only support *.bin file */

  binfile = fopen(argv[1], "rb");
  if (!binfile) {
    err_print("Can't open binary file '%s'", argv[1]);
    exit(EXIT_FAILURE);
  }

  sim = new_y64sim(MEM_SIZE);
  if (load_binfile(sim->m, binfile) < 0) {
    err_print("Failed to load binary file '%s'", argv[1]);
    free_y64sim(sim);
    exit(EXIT_FAILURE);
  }
  fclose(binfile);

  /* save initial register and memory stat */
  saver = dup_reg(sim->r);
  savem = dup_mem(sim->m);

  /* execute binary code step-by-step */
  for (step = 0; step < max_steps && e == STAT_AOK; step++)
    e = nexti(sim);

  /* print final stat of y64sim */
  printf("Stopped in %d steps at PC = 0x%lx.  Status '%s', CC %s\n", step,
         sim->pc, stat_name(e), cc_name(sim->cc));

  printf("Changes to registers:\n");
  diff_reg(saver, sim->r, stdout);

  printf("\nChanges to memory:\n");
  diff_mem(savem, sim->m, stdout);

  free_y64sim(sim);
  free_reg(saver);
  free_mem(savem);

  return 0;
}
