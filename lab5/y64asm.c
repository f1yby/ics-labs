#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "y64asm.h"

line_t *line_head = NULL;
line_t *line_tail = NULL;
int lineno = 0;

#define err_print(_s, _a...)                                                   \
  do {                                                                         \
    if (lineno < 0)                                                            \
      fprintf(stderr,                                                          \
              "[--]: "_s                                                       \
              "\n",                                                            \
              ##_a);                                                           \
    else                                                                       \
      fprintf(stderr,                                                          \
              "[L%d]: "_s                                                      \
              "\n",                                                            \
              lineno, ##_a);                                                   \
  } while (0)

int64_t vmaddr = 0; /* vm addr */
// int64_t maxaddr = 0;
/* register table */
const reg_t reg_table[REG_NONE] = {
    {"%rax", REG_RAX, 4}, {"%rcx", REG_RCX, 4}, {"%rdx", REG_RDX, 4},
    {"%rbx", REG_RBX, 4}, {"%rsp", REG_RSP, 4}, {"%rbp", REG_RBP, 4},
    {"%rsi", REG_RSI, 4}, {"%rdi", REG_RDI, 4}, {"%r8", REG_R8, 3},
    {"%r9", REG_R9, 3},   {"%r10", REG_R10, 4}, {"%r11", REG_R11, 4},
    {"%r12", REG_R12, 4}, {"%r13", REG_R13, 4}, {"%r14", REG_R14, 4}};
const reg_t *find_register(char *name) {
  int i;
  for (i = 0; i < REG_NONE; i++)
    if (!strncmp(name, reg_table[i].name, reg_table[i].namelen))
      return &reg_table[i];
  return NULL;
}

/* instruction set */
instr_t instr_set[] = {
    {"nop", 3, HPACK(I_NOP, F_NONE), 1},
    {"halt", 4, HPACK(I_HALT, F_NONE), 1},
    {"rrmovq", 6, HPACK(I_RRMOVQ, F_NONE), 2},
    {"cmovle", 6, HPACK(I_RRMOVQ, C_LE), 2},
    {"cmovl", 5, HPACK(I_RRMOVQ, C_L), 2},
    {"cmove", 5, HPACK(I_RRMOVQ, C_E), 2},
    {"cmovne", 6, HPACK(I_RRMOVQ, C_NE), 2},
    {"cmovge", 6, HPACK(I_RRMOVQ, C_GE), 2},
    {"cmovg", 5, HPACK(I_RRMOVQ, C_G), 2},
    {"irmovq", 6, HPACK(I_IRMOVQ, F_NONE), 10},
    {"rmmovq", 6, HPACK(I_RMMOVQ, F_NONE), 10},
    {"mrmovq", 6, HPACK(I_MRMOVQ, F_NONE), 10},
    {"addq", 4, HPACK(I_ALU, A_ADD), 2},
    {"subq", 4, HPACK(I_ALU, A_SUB), 2},
    {"andq", 4, HPACK(I_ALU, A_AND), 2},
    {"xorq", 4, HPACK(I_ALU, A_XOR), 2},
    {"jmp", 3, HPACK(I_JMP, C_YES), 9},
    {"jle", 3, HPACK(I_JMP, C_LE), 9},
    {"jl", 2, HPACK(I_JMP, C_L), 9},
    {"je", 2, HPACK(I_JMP, C_E), 9},
    {"jne", 3, HPACK(I_JMP, C_NE), 9},
    {"jge", 3, HPACK(I_JMP, C_GE), 9},
    {"jg", 2, HPACK(I_JMP, C_G), 9},
    {"call", 4, HPACK(I_CALL, F_NONE), 9},
    {"ret", 3, HPACK(I_RET, F_NONE), 1},
    {"pushq", 5, HPACK(I_PUSHQ, F_NONE), 2},
    {"popq", 4, HPACK(I_POPQ, F_NONE), 2},
    {".byte", 5, HPACK(I_DIRECTIVE, D_DATA), 1},
    {".word", 5, HPACK(I_DIRECTIVE, D_DATA), 2},
    {".long", 5, HPACK(I_DIRECTIVE, D_DATA), 4},
    {".quad", 5, HPACK(I_DIRECTIVE, D_DATA), 8},
    {".pos", 4, HPACK(I_DIRECTIVE, D_POS), 0},
    {".align", 6, HPACK(I_DIRECTIVE, D_ALIGN), 0},
    {NULL, 1, 0, 0} // end
};
bool_t need_regA(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_RRMOVQ:
  case I_RMMOVQ:
  case I_ALU:
  case I_PUSHQ:
  case I_POPQ:
    return TRUE;
  default:
    return FALSE;
  }
}
bool_t need_regB(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_MRMOVQ:
  case I_RRMOVQ:
  case I_IRMOVQ:
  case I_ALU:
    return TRUE;
  default:
    return FALSE;
  }
}

bool_t need_imm(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_IRMOVQ:
  case I_JMP:
  case I_CALL:
    return TRUE;
  default:
    return FALSE;
  }
}

bool_t need_delim(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_RRMOVQ:
  case I_IRMOVQ:
  case I_RMMOVQ:
  case I_MRMOVQ:
  case I_ALU:
    return TRUE;
  default:
    return FALSE;
  }
}

bool_t need_memA(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_MRMOVQ:
    return TRUE;
  default:
    return FALSE;
  }
}

bool_t need_memB(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_RMMOVQ:
    return TRUE;
  default:
    return FALSE;
  }
}

bool_t need_data(instr_t *i) {
  switch (HIGH(i->code)) {
  case I_DIRECTIVE:
    return TRUE;
  default:
    return FALSE;
  }
}

instr_t *find_instr(char *name) {
  int i;
  for (i = 0; instr_set[i].name; i++)
    if (strncmp(instr_set[i].name, name, instr_set[i].len) == 0)
      return &instr_set[i];
  return NULL;
}

/* symbol table (don't forget to init and finit it) */
symbol_t *symbol_table = NULL;

/*
 * find_symbol: scan table to find the symbol
 * args
 *     name: the name of symbol
 *
 * return
 *     symbol_t: the 'name' symbol
 *     NULL: not exist
 */
symbol_t *find_symbol(char *name) {
  symbol_t *s = symbol_table->next;
  while (s) {
    if (strcmp(name, s->name) == 0) {
      break;
    }
    s = s->next;
  }
  return s;
}

/*
 * add_symbol: add a new symbol to the symbol table
 * args
 *     name: the name of symbol
 *
 * return
 *     0: success
 *     -1: error, the symbol has exist
 */
int add_symbol(char *name) {
  /* check duplicate */
  if (find_symbol(name) != NULL) {
    return -1;
  }

  /* create new symbol_t (don't forget to free it)*/
  symbol_t *new_node = malloc(sizeof(symbol_t));

  /* add the new symbol_t to symbol table */
  new_node->name = name;
  new_node->next = symbol_table->next;
  new_node->addr = vmaddr;
  symbol_table->next = new_node;
  return 0;
}

/* relocation table (don't forget to init and finit it) */
reloc_t *relocation_table = NULL;
/*
 * add_reloc: add a new relocation to the relocation table
 * args
 *     name: the name of symbol
 */
void add_reloc(char *name, bin_t *bin) {
  /* create new reloc_t (don't forget to free it)*/
  reloc_t *r = malloc(sizeof(reloc_t));

  /* add the new reloc_t to relocation table */
  r->next = relocation_table->next;
  r->name = name;
  r->y64bin = bin;
  relocation_table->next = r;
}

/* macro for parsing y64 assembly code */
#define IS_DIGIT(s) ((*(s) >= '0' && *(s) <= '9') || *(s) == '-' || *(s) == '+')
#define IS_LETTER(s)                                                           \
  ((*(s) >= 'a' && *(s) <= 'z') || (*(s) >= 'A' && *(s) <= 'Z'))
#define IS_COMMENT(s) (*(s) == '#')
#define IS_REG(s) (*(s) == '%')
#define IS_IMM(s) (*(s) == '$')

#define IS_BLANK(s) (*(s) == ' ' || *(s) == '\t')
#define IS_END(s) (*(s) == '\0')

#define SKIP_BLANK(s)                                                          \
  do {                                                                         \
    while (!IS_END(s) && IS_BLANK(s))                                          \
      (s)++;                                                                   \
  } while (0)

/* return value from different parse_xxx function */
typedef enum {
  PARSE_ERR = -1,
  PARSE_REG,
  PARSE_DIGIT,
  PARSE_SYMBOL,
  PARSE_MEM,
  PARSE_DELIM,
  PARSE_INSTR,
  PARSE_LABEL
} parse_t;

/*
 * parse_instr: parse an expected data token (e.g., 'rrmovq')
 * args
 *     ptr: point to the start of string
 *     inst: point to the inst_t within instr_set
 *
 * return
 *     PARSE_INSTR: success, move 'ptr' to the first char after token,
 *                            and store the pointer of the instruction to 'inst'
 *     PARSE_ERR: error, the value of 'ptr' and 'inst' are undefined
 */
parse_t parse_instr(char **ptr, instr_t **inst) {
  /* skip the blank */
  char *p = *ptr;
  SKIP_BLANK(p);

  /* find_instr and check end */
  instr_t *i = find_instr(p);
  if (i == NULL) {
    return PARSE_ERR;
  }
  p += i->len;

  /* set 'ptr' and 'inst' */
  *ptr = p;
  *inst = i;
  return PARSE_INSTR;
}

/*
 * parse_delim: parse an expected delimiter token (e.g., ',')
 * args
 *     ptr: point to the start of string
 *
 * return
 *     PARSE_DELIM: success, move 'ptr' to the first char after token
 *     PARSE_ERR: error, the value of 'ptr' and 'delim' are undefined
 */
parse_t parse_delim(char **ptr, char delim) {
  /* skip the blank and check */
  char *p = *ptr;
  SKIP_BLANK(p);

  if (*p != delim) {
    return PARSE_ERR;
  }

  /* set 'ptr' */
  *ptr = p + 1;

  return PARSE_DELIM;
}

/*
 * parse_reg: parse an expected register token (e.g., '%rax')
 * args
 *     ptr: point to the start of string
 *     regid: point to the regid of register
 *
 * return
 *     PARSE_REG: success, move 'ptr' to the first char after token,
 *                         and store the regid to 'regid'
 *     PARSE_ERR: error, the value of 'ptr' and 'regid' are undefined
 */
parse_t parse_reg(char **ptr, regid_t *regid) {
  /* skip the blank and check */
  char *p = *ptr;
  SKIP_BLANK(p);

  /* find register */
  const reg_t *r = find_register(p);
  if (r == NULL) {
    return PARSE_ERR;
  }
  p += r->namelen;

  /* set 'ptr' and 'regid' */
  *regid = r->id;
  *ptr = p;

  return PARSE_REG;
}

/*
 * parse_symbol: parse an expected symbol token (e.g., 'Main')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *
 * return
 *     PARSE_SYMBOL: success, move 'ptr' to the first char after token,
 *                               and allocate and store name to 'name'
 *     PARSE_ERR: error, the value of 'ptr' and 'name' are undefined
 */
parse_t parse_symbol(char **ptr, char **name) {
  /* skip the blank and check */

  char *p = *ptr;
  SKIP_BLANK(p);
  if (!IS_LETTER(p)) {
    return PARSE_ERR;
  }
  char *l_start = p;
  while (IS_LETTER(p) || IS_DIGIT(p)) {
    ++p;
  }
  long len = p - l_start;
  if (len == 0) {
    return PARSE_ERR;
  }

  /* allocate name and copy to it */
  char *n = malloc(len + 1);
  strncpy(n, l_start, len + 1);
  n[len] = '\0';

  /* set 'ptr' and 'name' */
  *ptr = p;
  *name = n;

  return PARSE_SYMBOL;
}

/*
 * parse_digit: parse an expected digit token (e.g., '0x100')
 * args
 *     ptr: point to the start of string
 *     value: point to the value of digit
 *
 * return
 *     PARSE_DIGIT: success, move 'ptr' to the first char after token
 *                            and store the value of digit to 'value'
 *     PARSE_ERR: error, the value of 'ptr' and 'value' are undefined
 */
parse_t parse_digit(char **ptr, long *value) {
  /* skip the blank and check */
  char *p = *ptr;
  SKIP_BLANK(p);
  if (!IS_DIGIT(p)) {
    return PARSE_ERR;
  }
  /* calculate the digit, (NOTE: see strtoll()) */
  errno = 0;
  long v = strtoull(p, &p, 0);

  /* set 'ptr' and 'value' */

  *value = v;
  *ptr = p;

  return PARSE_DIGIT;
}

/*
 * parse_imm: parse an expected immediate token (e.g., '$0x100' or 'STACK')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *     value: point to the value of digit
 *
 * return
 *     PARSE_DIGIT: success, the immediate token is a digit,
 *                            move 'ptr' to the first char after token,
 *                            and store the value of digit to 'value'
 *     PARSE_SYMBOL: success, the immediate token is a symbol,
 *                            move 'ptr' to the first char after token,
 *                            and allocate and store name to 'name'
 *     PARSE_ERR: error, the value of 'ptr', 'name' and 'value' are undefined
 */
parse_t parse_imm(char **ptr, char **name, long *value) {
  /* skip the blank and check */
  char *p = *ptr;
  SKIP_BLANK(p);

  /* if IS_IMM, then parse the digit */
  if (IS_IMM(p)) {
    ++p;
    long v;
    if (parse_digit(&p, &v) == PARSE_DIGIT) {
      *value = v;
      *ptr = p;
      return PARSE_DIGIT;
    } else {
      return PARSE_ERR;
    }
  }

  /* if IS_LETTER, then parse the symbol */
  if (IS_LETTER(p)) {
    if (parse_symbol(&p, name) == PARSE_SYMBOL) {
      *ptr = p;
      return PARSE_SYMBOL;
    } else {
      return PARSE_ERR;
    }
  }

  /* set 'ptr' and 'name' or 'value' */

  return PARSE_ERR;
}

/*
 * parse_mem: parse an expected memory token (e.g., '8(%rbp)')
 * args
 *     ptr: point to the start of string
 *     value: point to the value of digit
 *     regid: point to the regid of register
 *
 * return
 *     PARSE_MEM: success, move 'ptr' to the first char after token,
 *                          and store the value of digit to 'value',
 *                          and store the regid to 'regid'
 *     PARSE_ERR: error, the value of 'ptr', 'value' and 'regid' are undefined
 */
parse_t parse_mem(char **ptr, long *value, regid_t *regid) {
  /* skip the blank and check */
  char *p = *ptr;
  SKIP_BLANK(p);

  /* calculate the digit and register, (ex: (%rbp) or 8(%rbp)) */
  long o = 0;
  parse_digit(&p, &o);
  if (parse_delim(&p, '(') == PARSE_ERR) {
    return PARSE_ERR;
  }
  regid_t r;
  if (parse_reg(&p, &r) == PARSE_ERR) {
    return PARSE_ERR;
  }
  if (parse_delim(&p, ')') == PARSE_ERR) {
    return PARSE_ERR;
  }

  /* set 'ptr', 'value' and 'regid' */
  *ptr = p;
  *value = o;
  *regid = r;
  return PARSE_MEM;
}

/*
 * parse_data: parse an expected data token (e.g., '0x100' or 'array')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *     value: point to the value of digit
 *
 * return
 *     PARSE_DIGIT: success, data token is a digit,
 *                            and move 'ptr' to the first char after token,
 *                            and store the value of digit to 'value'
 *     PARSE_SYMBOL: success, data token is a symbol,
 *                            and move 'ptr' to the first char after token,
 *                            and allocate and store name to 'name'
 *     PARSE_ERR: error, the value of 'ptr', 'name' and 'value' are undefined
 */
parse_t parse_data(char **ptr, char **name, long *value) {
  /* skip the blank and check */
  char *p = *ptr;
  SKIP_BLANK(p);

  /* if IS_DIGIT, then parse the digit */
  if (IS_DIGIT(p)) {
    long d = 0;
    if (parse_digit(&p, &d) == PARSE_DIGIT) {
      *value = d;
      *ptr = p;
      return PARSE_DIGIT;
    }
  }

  /* if IS_LETTER, then parse the symbol */
  if (IS_LETTER(p)) {
    char *n;
    if (parse_symbol(&p, &n) == PARSE_SYMBOL) {
      *name = n;
      *ptr = p;
      return PARSE_SYMBOL;
    }
  }

  /* set 'ptr', 'name' and 'value' */
  return PARSE_ERR;
}

/*
 * parse_label: parse an expected label token (e.g., 'Loop:')
 * args
 *     ptr: point to the start of string
 *     name: point to the name of symbol (should be allocated in this function)
 *
 * return
 *     PARSE_LABEL: success, move 'ptr' to the first char after token
 *                            and allocate and store name to 'name'
 *     PARSE_ERR: error, the value of 'ptr' is undefined
 */
parse_t parse_label(char **ptr, char **name) {
  /* skip the blank and check */
  char *l;
  char *p = *ptr;
  if (parse_symbol(&p, &l) == PARSE_ERR) {
    return PARSE_ERR;
  }
  SKIP_BLANK(p);
  if (*p != ':') {
    free(l);
    return PARSE_ERR;
  }

  /* allocate name and copy to it */

  /* set 'ptr' and 'name' */
  *ptr = p + 1;
  *name = l;

  return PARSE_LABEL;
}

/*
 * parse_line: parse a line of y64 code (e.g., 'Loop: mrmovq (%rcx), %rsi')
 * (you could combine above parse_xxx functions to do it)
 * args
 *     line: point to a line_t data with a line of y64 assembly code
 *
 * return
 *     TYPE_XXX: success, fill line_t with assembled y64 code
 *     TYPE_ERR: error, try to print err information
 */
type_t parse_line(line_t *line) {

  /* when finish parse an instruction or lable, we still need to continue check
   * e.g.,
   *  Loop: mrmovl (%rbp), %rcx
   *           call SUM  #invoke SUM function */

  /* skip blank and check IS_END */
  char *s = line->y64asm;
  SKIP_BLANK(s);
  if (IS_END(s)) {
    return TYPE_COMM;
  }

  /* is a comment ? */
  if (IS_COMMENT(s)) {
    return TYPE_COMM;
  }

  /* is a label ? */
  char *n;
  if (parse_label(&s, &n) == PARSE_LABEL) {
    if (add_symbol(n)) {
      err_print("Dup symbol:%s", n);
      return TYPE_ERR;
    }
  }

  /* is an instruction ? */
  instr_t *i;
  if (parse_instr(&s, &i) == PARSE_INSTR) {
    if (!IS_BLANK(s) && !IS_COMMENT(s) && !IS_END(s)) {
      return TYPE_ERR;
    }

    /* set type and y64bin */
    line->type = TYPE_INS;
    line->y64bin.addr = vmaddr;
    int offset = 0;
    line->y64bin.codes[0] = i->code;
    offset += 1;
    line->y64bin.bytes = i->bytes;

    /* update vmaddr */
    vmaddr += i->bytes;

    /* parse the rest of instruction according to the itype */
    // INSTR[SPACE,END,COMMENT](REG_A,MEM_A)(IMM)(,)(REG_B,MEM_B)(DIGIT)[END,COMMENT]
    regid_t regA = REG_NONE;
    regid_t regB = REG_NONE;
    bool_t reg_order = TRUE;
    char *label = NULL;
    int64_t imm = 0;
    // u_int64_t data;
    bool_t has_reg = FALSE;
    bool_t has_imm = FALSE;
    if (need_regA(i)) {
      if (parse_reg(&s, &regA) == PARSE_ERR) {
        err_print("Invalid REG");
        return TYPE_ERR;
      }
      has_reg = TRUE;
    }

    if (need_memA(i)) {
      if (parse_mem(&s, &imm, &regA) == PARSE_ERR) {
        err_print("Invalid MEM");
        return TYPE_ERR;
      }
      reg_order = FALSE;
      has_reg = TRUE;
      has_imm = TRUE;
    }

    if (need_imm(i)) {
      switch (parse_imm(&s, &label, &imm)) {
      case PARSE_DIGIT:
        break;
      case PARSE_SYMBOL:
        add_reloc(label, &line->y64bin);
        break;
      default:
        switch (HIGH(i->code)) {
        case I_JMP:
          err_print("Invalid DEST");
          break;
        default:
          err_print("Invalid Immediate");
          break;
        }

        return TYPE_ERR;
      }
      has_imm = TRUE;
    }

    if (need_delim(i)) {
      if (parse_delim(&s, ',') == PARSE_ERR) {
        err_print("Invalid ','");
        return TYPE_ERR;
      }
    }

    if (need_regB(i)) {
      if (parse_reg(&s, &regB) == PARSE_ERR) {
        return TYPE_ERR;
      }
      has_reg = TRUE;
    }

    if (need_memB(i)) {
      if (parse_mem(&s, &imm, &regB) == PARSE_ERR) {
        return TYPE_ERR;
      }
      has_reg = TRUE;
      has_imm = TRUE;
    }
    if (need_data(i)) {
      switch (parse_data(&s, &label, &imm)) {
      case PARSE_DIGIT:
        break;
      case PARSE_SYMBOL:
        add_reloc(label, &line->y64bin);
        break;
      default:
        // err_print("Invalid DEST");
        return TYPE_ERR;
      }
      has_imm = TRUE;
      offset = 0;
    }

    // fill y64-bin
    // I_CODE|F_CODE(REG_A|REG_B)(IMM)(DIGIT)

    if (has_reg) {
      if (reg_order) {
        line->y64bin.codes[offset] = HPACK(regA, regB);
      } else {
        line->y64bin.codes[offset] = HPACK(regB, regA);
      }

      ++offset;
    }
    if (has_imm) {

      *(int64_t *)(line->y64bin.codes + offset) = imm;
    }
    if (i->code == HPACK(I_DIRECTIVE, D_POS)) {
      vmaddr = imm;
    }
    if (i->code == HPACK(I_DIRECTIVE, D_ALIGN)) {
      if (vmaddr % imm != 0) {
        vmaddr += imm - vmaddr % imm;
      }
    }
  }
  SKIP_BLANK(s);
  if (!IS_END(s) && !IS_COMMENT(s)) {
    return TYPE_ERR;
  }
  return line->type;
}

/*
 * assemble: assemble an y64 file (e.g., 'asum.ys')
 * args
 *     in: point to input file (an y64 assembly file)
 *
 * return
 *     0: success, assmble the y64 file to a list of line_t
 *     -1: error, try to print err information (e.g., instr type and line
 * number)
 */
int assemble(FILE *in) {
  static char asm_buf[MAX_INSLEN]; /* the current line of asm code */
  line_t *line;
  unsigned long slen;
  char *y64asm;

  /* read y64 code line-by-line, and parse them to generate raw y64 binary code
   * list */
  while (fgets(asm_buf, MAX_INSLEN, in) != NULL) {
    slen = strlen(asm_buf);
    while ((asm_buf[slen - 1] == '\n') || (asm_buf[slen - 1] == '\r')) {
      asm_buf[--slen] = '\0'; /* replace terminator */
    }

    /* store y64 assembly code */
    y64asm = (char *)malloc(sizeof(char) * (slen + 1)); // free in finit
    strcpy(y64asm, asm_buf);

    line = (line_t *)malloc(sizeof(line_t)); // free in finit
    memset(line, '\0', sizeof(line_t));

    line->type = TYPE_COMM;
    line->y64asm = y64asm;
    line->next = NULL;
    line_tail->next = line;
    line_tail = line;
    lineno++;

    if (parse_line(line) == TYPE_ERR) {
      return -1;
    }
  }

  lineno = -1;
  return 0;
}

/*
 * relocate: relocate the raw y64 binary code with symbol address
 *
 * return
 *     0: success
 *     -1: error, try to print err information (e.g., addr and symbol)
 */
int relocate(void) {
  reloc_t *rtmp = NULL;

  rtmp = relocation_table->next;
  while (rtmp) {
    /* find symbol */
    symbol_t *s = find_symbol(rtmp->name);
    if (!s) {
      err_print("Unknown symbol:'%s'", rtmp->name);
      return -1;
    }

    /* relocate y64bin according itype */
    int offset = rtmp->y64bin->bytes - 8;
    *(long *)(rtmp->y64bin->codes + offset) = s->addr;

    /* next */
    rtmp = rtmp->next;
  }

  return 0;
}

/*
 * binfile: generate the y64 binary file
 * args
 *     out: point to output file (an y64 binary file)
 *
 * return
 *     0: success
 *     -1: error
 */
int binfile(FILE *out) {
  /* prepare image with y64 binary code */
  line_t *l = line_head->next;
  // fwrite("\0", maxaddr, 1, out);
  /* binary write y64 code to output file (NOTE: see fwrite()) */
  while (l != NULL) {
    fseek(out, l->y64bin.addr, 0);
    fwrite(l->y64bin.codes, l->y64bin.bytes, 1, out);
    l = l->next;
  }

  return 0;
}

/* whether print the readable output to screen or not ? */
bool_t screen = FALSE;

static void hexstuff(char *dest, int value, int len) {
  int i;
  for (i = 0; i < len; i++) {
    char c;
    int h = (value >> 4 * i) & 0xF;
    c = h < 10 ? h + '0' : h - 10 + 'a';
    dest[len - i - 1] = c;
  }
}

void print_line(line_t *line) {
  char buf[64];

  /* line format: 0xHHH: cccccccccccc | <line> */
  if (line->type == TYPE_INS) {
    bin_t *y64bin = &line->y64bin;
    int i;

    strcpy(buf, "  0x000:                      | ");

    hexstuff(buf + 4, y64bin->addr, 3);
    if (y64bin->bytes > 0)
      for (i = 0; i < y64bin->bytes; i++)
        hexstuff(buf + 9 + 2 * i, y64bin->codes[i] & 0xFF, 2);
  } else {
    strcpy(buf, "                              | ");
  }

  printf("%s%s\n", buf, line->y64asm);
}

/*
 * print_screen: dump readable binary and assembly code to screen
 * (e.g., Figure 4.8 in ICS book)
 */
void print_screen(void) {
  line_t *tmp = line_head->next;
  while (tmp != NULL) {
    print_line(tmp);
    tmp = tmp->next;
  }
}

/* init and finit */
void init(void) {
  relocation_table = (reloc_t *)malloc(sizeof(reloc_t)); // free in finit
  memset(relocation_table, 0, sizeof(reloc_t));

  symbol_table = (symbol_t *)malloc(sizeof(symbol_t)); // free in finit
  memset(symbol_table, 0, sizeof(symbol_t));

  line_head = (line_t *)malloc(sizeof(line_t)); // free in finit
  memset(line_head, 0, sizeof(line_t));
  line_tail = line_head;
  lineno = 0;
}

void finit(void) {
  reloc_t *rtmp = NULL;
  do {
    rtmp = relocation_table->next;
    if (relocation_table->name)
      free(relocation_table->name);
    free(relocation_table);
    relocation_table = rtmp;
  } while (relocation_table);

  symbol_t *stmp = NULL;
  do {
    stmp = symbol_table->next;
    if (symbol_table->name)
      free(symbol_table->name);
    free(symbol_table);
    symbol_table = stmp;
  } while (symbol_table);

  line_t *ltmp = NULL;
  do {
    ltmp = line_head->next;
    if (line_head->y64asm)
      free(line_head->y64asm);
    free(line_head);
    line_head = ltmp;
  } while (line_head);
}

static void usage(char *pname) {
  printf("Usage: %s [-v] file.ys\n", pname);
  printf("   -v print the readable output to screen\n");
  exit(0);
}

int main(int argc, char *argv[]) {
  unsigned long rootlen;
  char infname[512];
  char outfname[512];
  int nextarg = 1;
  FILE *in = NULL, *out = NULL;

  if (argc < 2)
    usage(argv[0]);

  if (argv[nextarg][0] == '-') {
    char flag = argv[nextarg][1];
    switch (flag) {
    case 'v':
      screen = TRUE;
      nextarg++;
      break;
    default:
      usage(argv[0]);
    }
  }

  /* parse input file name */
  rootlen = strlen(argv[nextarg]) - 3;
  /* only support the .ys file */
  if (strcmp(argv[nextarg] + rootlen, ".ys") != 0)
    usage(argv[0]);

  if (rootlen > 500) {
    err_print("File name too long");
    exit(1);
  }

  /* init */
  init();

  /* assemble .ys file */
  strncpy(infname, argv[nextarg], rootlen);
  strcpy(infname + rootlen, ".ys");
  in = fopen(infname, "r");
  if (!in) {
    err_print("Can't open input file '%s'", infname);
    exit(1);
  }

  if (assemble(in) < 0) {
    err_print("Assemble y64 code error");
    fclose(in);
    exit(1);
  }
  fclose(in);

  /* relocate binary code */
  if (relocate() < 0) {
    err_print("Relocate binary code error");
    exit(1);
  }

  /* generate .bin file */
  strncpy(outfname, argv[nextarg], rootlen);
  strcpy(outfname + rootlen, ".bin");
  out = fopen(outfname, "wb");
  if (!out) {
    err_print("Can't open output file '%s'", outfname);
    exit(1);
  }

  if (binfile(out) < 0) {
    err_print("Generate binary file error");
    fclose(out);
    exit(1);
  }
  fclose(out);

  /* print to screen (.yo file) */
  if (screen)
    print_screen();

  /* finit */
  finit();
  return 0;
}
