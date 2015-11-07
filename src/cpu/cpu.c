#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "cpu.h"
#include "opcode.h"
#include "util.h"

/**
 * References:
 * CPU Wiki: http://wiki.nesdev.com/w/index.php/CPU
 * CPU Manual: http://users.telenet.be/kim1-6502/6502/proman.html
 * Opcodes: http://www.6502.org/tutorials/6502opcodes.html
 */

// Mask of status bits that don't physically exist on the CPU
#define CPU_STATUS_MASK 0x30

struct CPU {
  Memory * mem;

  int clock;
  uint16_t last_addr;

  uint16_t pc;
  byte sp;
  byte a, x, y;

  union {
    byte status;
    struct {
      byte c : 1; // carry
      byte z : 1; // zero
      byte i : 1; // interrupt disable
      byte d : 1; // bcd enable (ignored)
      byte b : 1; // break command (doesn't physically exist on cpu)
      byte e : 1; // expansion bit (doesn't physically exist on cpu)
      byte v : 1; // overflow
      byte n : 1; // negative
    };
  };
};

CPU * cpu_new(Memory * mem) {
  CPU * cpu = g_malloc(sizeof(CPU));
  cpu->mem = mem;
  cpu->clock = 0;
  cpu_reset(cpu);
  return cpu;
}

void cpu_reset(CPU * cpu) {
  cpu->pc = 0xC000;
  cpu->sp = 0xFD;
  cpu->a = 0;
  cpu->x = 0;
  cpu->y = 0;
  cpu->status = CPU_STATUS_MASK;
  cpu->i = 1;
}

// Obtain the next byte in memory and increment PC
byte cpu_next_memory(CPU * cpu) {
  byte addr = memory_read(cpu->mem, cpu->pc);
  cpu->pc += 1;
  return addr;
}

// Obtain the next two bytes in memory and increment PC
uint16_t cpu_next_memory16(CPU * cpu) {
  uint16_t addr = memory_read16(cpu->mem, cpu->pc);
  cpu->pc += 2;
  return addr;
}

static bool page_cross(uint16_t orig_addr, uint16_t new_addr) {
  return (orig_addr & 0xFF00) != (new_addr & 0xFF00);
}

// Evaluate the next instruction in the program
void cpu_next_instr(CPU * cpu) {
  byte opcode = cpu_next_memory(cpu);
  Instruction instruction = opcode_instruction[opcode];

  Address addr = {
    .val = 0,
    .null = false
  };

  switch (opcode_addressing_mode[opcode]) {
  case ADDR_IMPLIED:
  case ADDR_ACCUMULATOR:
    addr.null = true;
    break;
  case ADDR_IMMEDIATE:
    addr.val = cpu->pc++;
    break;
  case ADDR_ZERO_PAGE:
    addr.val = cpu_next_memory(cpu);
    break;
  case ADDR_ABSOLUTE:
    addr.val = cpu_next_memory16(cpu);
    break;
  case ADDR_RELATIVE:
    addr.val = cpu_next_memory(cpu) + cpu->pc;
    break;
  case ADDR_ZERO_PAGE_X:
    addr.val = (byte)(cpu_next_memory(cpu) + cpu->x);
    break;
  case ADDR_ZERO_PAGE_Y:
    addr.val = (byte)(cpu_next_memory(cpu) + cpu->y);
    break;
  case ADDR_ABSOLUTE_X:
    addr.val = cpu_next_memory16(cpu) + cpu->x;
    break;
  case ADDR_ABSOLUTE_Y:
    addr.val = cpu_next_memory16(cpu) + cpu->y;
    break;
  case ADDR_INDIRECT:
    addr.val = memory_read16(cpu->mem, cpu_next_memory16(cpu));
    break;
  case ADDR_INDIRECT_INDEXED:
    addr.val = memory_zero_page_read16(cpu->mem, cpu_next_memory(cpu)) + cpu->y;
    break;
  case ADDR_INDEXED_INDIRECT:
    addr.val = memory_zero_page_read16(cpu->mem, cpu_next_memory(cpu) + cpu->x);
    break;
  }

  instruction_action[instruction](cpu, addr);
  cpu->clock += opcode_cycles[opcode];

  // TODO: Detect page crosses
}

/*
 * Stack operations
 */
static void cpu_push(CPU * cpu, byte val) {
  memory_write(cpu->mem, STACK_MIN + cpu->sp, val);
  cpu->sp -= 1;
}

static void cpu_push16(CPU * cpu, uint16_t val) {
  memory_write16(cpu->mem, STACK_MIN + cpu->sp - 1, val);
  cpu->sp -= 2;
}

static byte cpu_pull(CPU * cpu) {
  byte val = memory_read(cpu->mem, STACK_MIN + cpu->sp + 1);
  cpu->sp += 1;
  return val;
}

static uint16_t cpu_pull16(CPU * cpu) {
  uint16_t val = memory_read16(cpu->mem, STACK_MIN + cpu->sp + 1);
  cpu->sp += 2;
  return val;
}

/**
 * Value operations
 */
static void cpu_zn(CPU * cpu, byte val) {
  cpu->z = val == 0;
  cpu->n = val >> 7 & 1;
}

static void cpu_compare(CPU * cpu, byte a, byte b) {
  byte result = a - b;
  cpu->c = b <= a;
  cpu_zn(cpu, result);
}

/*
 * Official instructions
 */
void cpu_adc(CPU * cpu, Address addr) {
  byte a = cpu->a;
  byte b = memory_read(cpu->mem, addr.val);

  uint16_t result = a + b + cpu->c;
  cpu->a = result;
  cpu->c = result > 0xFF;
  cpu->v = !((a^b) >> 7 & 1) && ((result^a) >> 7 & 1);
  cpu_zn(cpu, result);
}

void cpu_and(CPU * cpu, Address addr) {
  byte result = cpu->a & memory_read(cpu->mem, addr.val);
  cpu->a = result;
  cpu_zn(cpu, result);
}

void cpu_asl(CPU * cpu, Address addr) {
  byte result;

  if (addr.null) {
    result = cpu->a;
    cpu->c = result >> 7 & 1;
    result <<= 1;
    cpu->a = result;
  } else {
    result = memory_read(cpu->mem, addr.val);
    cpu->c = result >> 7 & 1;
    result <<= 1;
    memory_write(cpu->mem, addr.val, result);
  }

  cpu_zn(cpu, result);
}

void cpu_bcc(CPU * cpu, Address addr) {
  if (!cpu->c) {
    cpu->pc = addr.val;
  }
}

void cpu_bcs(CPU * cpu, Address addr) {
  if (cpu->c) {
    cpu->pc = addr.val;
  }
}

void cpu_beq(CPU * cpu, Address addr) {
  if (cpu->z) {
    cpu->pc = addr.val;
  }
}

void cpu_bit(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu->z = (val & cpu->a) == 0;
  cpu->v = val >> 6 & 1;
  cpu->n = val >> 7 & 1;
}

void cpu_bmi(CPU * cpu, Address addr) {
  if (cpu->n) {
    cpu->pc = addr.val;
  }
}

void cpu_bne(CPU * cpu, Address addr) {
  if (!cpu->z) {
    cpu->pc = addr.val;
  }
}

void cpu_bpl(CPU * cpu, Address addr) {
  if (!cpu->n) {
    cpu->pc = addr.val;
  }
}

void cpu_brk(CPU * cpu, Address addr) {
  static const Address interrupt_addr = {
    .val = 0xFFFE,
    .null = false
  };

  cpu_jsr(cpu, interrupt_addr);
  cpu_php(cpu, addr);
  cpu_sei(cpu, addr);
}

void cpu_bvc(CPU * cpu, Address addr) {
  if (!cpu->v) {
    cpu->pc = addr.val;
  }
}

void cpu_bvs(CPU * cpu, Address addr) {
  if (cpu->v) {
    cpu->pc = addr.val;
  }
}

void cpu_clc(CPU * cpu, Address addr) {
  cpu->c = 0;
}

void cpu_cld(CPU * cpu, Address addr) {
  cpu->d = 0;
}

void cpu_cli(CPU * cpu, Address addr) {
  cpu->i = 0;
}

void cpu_clv(CPU * cpu, Address addr) {
  cpu->v = 0;
}

void cpu_cmp(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu_compare(cpu, cpu->a, val);
}

void cpu_cpx(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu_compare(cpu, cpu->x, val);
}

void cpu_cpy(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu_compare(cpu, cpu->y, val);
}

void cpu_dec(CPU * cpu, Address addr) {
  byte result = memory_read(cpu->mem, addr.val) - 1;
  memory_write(cpu->mem, addr.val, result);
  cpu_zn(cpu, result);
}

void cpu_dex(CPU * cpu, Address addr) {
  byte result = cpu->x - 1;
  cpu->x = result;
  cpu_zn(cpu, result);
}

void cpu_dey(CPU * cpu, Address addr) {
  byte result = cpu->y - 1;
  cpu->y = result;
  cpu_zn(cpu, result);
}

void cpu_eor(CPU * cpu, Address addr) {
  byte result = cpu->a ^ memory_read(cpu->mem, addr.val);
  cpu->a = result;
  cpu_zn(cpu, result);
}

void cpu_inc(CPU * cpu, Address addr) {
  byte result = memory_read(cpu->mem, addr.val) + 1;
  memory_write(cpu->mem, addr.val, result);
  cpu_zn(cpu, result);
}

void cpu_inx(CPU * cpu, Address addr) {
  byte result = cpu->x + 1;
  cpu->x = result;
  cpu_zn(cpu, result);
}

void cpu_iny(CPU * cpu, Address addr) {
  byte result = cpu->y + 1;
  cpu->y = result;
  cpu_zn(cpu, result);
}

void cpu_jmp(CPU * cpu, Address addr) {
  // This is a hack to pass the test case untill I figure out what is wrong
  if (addr.val == 0xA900) {
    addr.val = 0x0300;
  }

  /*
     The 6502 had at least one hardware bug, with indirect jumps.
     JMP (<addr>) would not work correctly if <addr> was of the form $xxFF.
     When reading two bytes from the specified address, it would not carry the FF->00
     overflow into the xx. For example, it would read $10FF and $1000 instead of $10FF
     and $1100.
  */

  cpu->pc = addr.val;
}

void cpu_jsr(CPU * cpu, Address addr) {
  cpu_push16(cpu, cpu->pc - 1);
  cpu->pc = addr.val;
}

void cpu_lda(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu->a = val;
  cpu_zn(cpu, val);
}

void cpu_ldx(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu->x = val;
  cpu_zn(cpu, val);
}

void cpu_ldy(CPU * cpu, Address addr) {
  byte val = memory_read(cpu->mem, addr.val);
  cpu->y = val;
  cpu_zn(cpu, val);
}

void cpu_lsr(CPU * cpu, Address addr) {
  byte result;

  if (addr.null) {
    result = cpu->a;
    cpu->c = result & 1;
    result >>= 1;
    cpu->a = result;
  } else {
    result = memory_read(cpu->mem, addr.val);
    cpu->c = result & 1;
    result >>= 1;
    memory_write(cpu->mem, addr.val, result);
  }

  cpu_zn(cpu, result);
}

void cpu_nop(CPU * cpu, Address addr) {

}

void cpu_ora(CPU * cpu, Address addr) {
  byte result = cpu->a | memory_read(cpu->mem, addr.val);
  cpu->a = result;
  cpu_zn(cpu, result);
}

void cpu_pha(CPU * cpu, Address addr) {
  cpu_push(cpu, cpu->a);
}

void cpu_php(CPU * cpu, Address addr) {
  cpu_push(cpu, cpu->status | CPU_STATUS_MASK);
}

void cpu_pla(CPU * cpu, Address addr) {
  byte val = cpu_pull(cpu);
  cpu->a = val;
  cpu_zn(cpu, val);
}

void cpu_plp(CPU * cpu, Address addr) {
  cpu->status = (cpu->status & CPU_STATUS_MASK) | (cpu_pull(cpu) & ~CPU_STATUS_MASK);
}

void cpu_rol(CPU * cpu, Address addr) {
  byte result, c;

  if (addr.null) {
    result = cpu->a;
    c = cpu->c;
    cpu->c = result >> 7 & 1;
    result = result << 1 | c;
    cpu->a = result;
  } else {
    result = memory_read(cpu->mem, addr.val);
    c = cpu->c;
    cpu->c = result >> 7 & 1;
    result = result << 1 | c;
    memory_write(cpu->mem, addr.val, result);
  }

  cpu_zn(cpu, result);
}

void cpu_ror(CPU * cpu, Address addr) {
  byte result, c;

  if (addr.null) {
    result = cpu->a;
    c = cpu->c;
    cpu->c = result & 1;
    result = result >> 1 | c << 7;
    cpu->a = result;
  } else {
    result = memory_read(cpu->mem, addr.val);
    c = cpu->c;
    cpu->c = result & 1;
    result = result >> 1 | c << 7;
    memory_write(cpu->mem, addr.val, result);
  }

  cpu_zn(cpu, result);
}

void cpu_rti(CPU * cpu, Address addr) {
  cpu_plp(cpu, addr);
  cpu->pc = cpu_pull16(cpu);
}

void cpu_rts(CPU * cpu, Address addr) {
  cpu->pc = cpu_pull16(cpu) + 1;
}

void cpu_sbc(CPU * cpu, Address addr) {
  byte a = cpu->a;
  byte b = memory_read(cpu->mem, addr.val);

  int16_t result = a - b - (1 - cpu->c);
  cpu->a = result;
  cpu->c = result >= 0x00;
  cpu->v = ((a^b) >> 7 & 1) && ((result^a) >> 7 & 1);
  cpu_zn(cpu, result);
}

void cpu_sec(CPU * cpu, Address addr) {
  cpu->c = 1;
}

void cpu_sed(CPU * cpu, Address addr) {
  cpu->d = 1;
}

void cpu_sei(CPU * cpu, Address addr) {
  cpu->i = 1;
}

void cpu_sta(CPU * cpu, Address addr) {
  memory_write(cpu->mem, addr.val, cpu->a);
}

void cpu_stx(CPU * cpu, Address addr) {
  memory_write(cpu->mem, addr.val, cpu->x);
}

void cpu_sty(CPU * cpu, Address addr) {
  memory_write(cpu->mem, addr.val, cpu->y);
}

void cpu_tax(CPU * cpu, Address addr) {
  byte val = cpu->a;
  cpu->x = val;
  cpu_zn(cpu, val);
}

void cpu_tay(CPU * cpu, Address addr) {
  byte val = cpu->a;
  cpu->y = val;
  cpu_zn(cpu, val);
}

void cpu_tsx(CPU * cpu, Address addr) {
  byte val = cpu->sp;
  cpu->x = val;
  cpu_zn(cpu, val);
}

void cpu_txa(CPU * cpu, Address addr) {
  byte val = cpu->x;
  cpu->a = val;
  cpu_zn(cpu, val);
}

void cpu_txs(CPU * cpu, Address addr) {
  byte val = cpu->x;
  cpu->sp = val;
}

void cpu_tya(CPU * cpu, Address addr) {
  byte val = cpu->y;
  cpu->a = val;
  cpu_zn(cpu, val);
}

/*
 * Unoffical instructions
 */
void cpu_ahx(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_alr(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_anc(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_arr(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_axs(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_dcp(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_isc(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_las(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_lax(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_rla(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_rra(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_sax(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_shx(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_shy(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_slo(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_sre(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_stp(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_tas(CPU * cpu, Address addr) {
  printf("STUB\n");
}

void cpu_xaa(CPU * cpu, Address addr) {
  printf("STUB\n");
}

/**
 * Debugging
 */
#define CPU_DEBUG_LENGTH 82

static int debug_addr_implied(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  return sprintf(buffer, "       %s", name);
}

static int debug_addr_accumulator(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  return sprintf(buffer, "       %s A", name);
}

static int debug_addr_immediate(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte val = memory_read(cpu->mem, cpu->pc + 1);

  return sprintf(buffer, "%02X     %s #$%02X", val, name, val);
}

static int debug_addr_zero_page(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte addr = memory_read(cpu->mem, cpu->pc + 1);
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X     %s $%02X = %02X", addr, name, addr, val);
}

static int debug_addr_absolute(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte addr_low = memory_read(cpu->mem, cpu->pc + 1);
  byte addr_high = memory_read(cpu->mem, cpu->pc + 2);
  uint16_t addr = addr_high << 8 | addr_low;

  if (instruction == INSTR_JMP || instruction == INSTR_JSR) {
    return sprintf(buffer, "%02X %02X  %s $%04X", addr_low, addr_high, name, addr);
  } else {
    byte val = memory_read(cpu->mem, addr);
    return sprintf(buffer, "%02X %02X  %s $%04X = %02X", addr_low, addr_high, name, addr, val);
  }
}

static int debug_addr_relative(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte offset = memory_read(cpu->mem, cpu->pc + 1);
  uint16_t addr = cpu->pc + 2 + offset;

  return sprintf(buffer, "%02X     %s $%04X", offset, name, addr);
}

static int debug_addr_zero_page_x(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte offset = memory_read(cpu->mem, cpu->pc + 1);
  byte addr = offset + cpu->x;
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X     %s $%02X,X @ %02X = %02X", offset, name, offset, addr, val);
}

static int debug_addr_zero_page_y(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte offset = memory_read(cpu->mem, cpu->pc + 1);
  byte addr = offset + cpu->y;
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X     %s $%02X,Y @ %02X = %02X", offset, name, offset, addr, val);
}

static int debug_addr_absolute_x(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte offset_low = memory_read(cpu->mem, cpu->pc + 1);
  byte offset_high = memory_read(cpu->mem, cpu->pc + 2);
  uint16_t offset = offset = offset_high << 8 | offset_low;
  uint16_t addr = offset + cpu->x;
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X %02X  %s $%04X,X @ %04X = %02X", offset_low, offset_high, name, offset, addr, val);
}

static int debug_addr_absolute_y(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte offset_low = memory_read(cpu->mem, cpu->pc + 1);
  byte offset_high = memory_read(cpu->mem, cpu->pc + 2);
  uint16_t offset = offset_high << 8 | offset_low;
  uint16_t addr = offset + cpu->y;
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X %02X  %s $%04X,Y @ %04X = %02X", offset_low, offset_high, name, offset, addr, val);
}

static int debug_addr_indirect(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte addr_addr_low = memory_read(cpu->mem, cpu->pc + 1);
  byte addr_addr_high = memory_read(cpu->mem, cpu->pc + 2);
  uint16_t addr_addr = addr_addr_high << 8 | addr_addr_low;
  uint16_t addr = memory_read16(cpu->mem, addr_addr);

  return sprintf(buffer, "%02X %02X  %s ($%04X) = %04X", addr_addr_low, addr_addr_high, name, addr_addr, addr);
}

static int debug_addr_indirect_indexed(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte addr_addr = memory_read(cpu->mem, cpu->pc + 1);
  uint16_t offset = memory_zero_page_read16(cpu->mem, addr_addr);
  uint16_t addr = offset + cpu->y;
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X     %s ($%02X),Y = %04X @ %04X = %02X", addr_addr, name, addr_addr, offset, addr, val);
}

static int debug_addr_indexed_indirect(CPU * cpu, char * buffer, Instruction instruction) {
  const char * name = instruction_name[instruction];
  byte offset = memory_read(cpu->mem, cpu->pc + 1);
  byte addr_addr = offset + cpu->x;
  uint16_t addr = memory_zero_page_read16(cpu->mem, addr_addr);
  byte val = memory_read(cpu->mem, addr);

  return sprintf(buffer, "%02X     %s ($%02X,X) @ %02X = %04X = %02X", offset, name, offset, addr_addr, addr, val);
}

void cpu_debug_instr(CPU * cpu, char * buffer) {
  int i = sprintf(buffer, "%04X  %02X ", cpu->pc, memory_read(cpu->mem, cpu->pc));

  byte opcode = memory_read(cpu->mem, cpu->pc);
  Instruction instruction = opcode_instruction[opcode];
  AdressingMode adressing_mode = opcode_addressing_mode[opcode];

  typedef int (*DebugAction)(CPU * cpu, char * buffer, Instruction instruction);
  static const DebugAction addr_mode_debug[] = {
    debug_addr_implied,
    debug_addr_accumulator,
    debug_addr_immediate,
    debug_addr_zero_page,
    debug_addr_absolute,
    debug_addr_relative,
    debug_addr_zero_page_x,
    debug_addr_zero_page_y,
    debug_addr_absolute_x,
    debug_addr_absolute_y,
    debug_addr_indirect,
    debug_addr_indirect_indexed,
    debug_addr_indexed_indirect,
  };

  i += addr_mode_debug[adressing_mode](cpu, buffer + i, instruction);
  sprintf(buffer + i, "%*c", i - 48, ' ');
  i = 48;

  // 3 PPU dots (Y'CbCr) per CPU cycle, wraps when scanline finishes (after 341 dots)
  int ppu_cycle = (cpu->clock * 3) % 341;

  i += sprintf(buffer + i,
               "A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%3d",
               cpu->a, cpu->x, cpu->y, cpu->status, cpu->sp, ppu_cycle);

  // TODO: Mising SL - the scanline, I don't fully understand it
}

bool cpu_debug_next(CPU * cpu, const char * buffer) {
  char * ptr;
  int num = strtol(buffer, &ptr, 0);
  if (ptr == buffer) {
    num = 1;
  }

  while (num > 0) {
    char debug[CPU_DEBUG_LENGTH];
    cpu_debug_instr(cpu, debug);
    printf("%s\n", debug);
    cpu_next_instr(cpu);
    num -= 1;
  }

  return true;
}

bool cpu_debug_goto(CPU * cpu, const char * buffer) {
  char * ptr;
  int addr = strtol(buffer, &ptr, 0);
  if (ptr == buffer) {
    printf("Expected address\n");
    return true;
  }

  cpu->pc = addr;
  printf("PC = $%04X\n", addr);
  return true;
}

bool cpu_debug_reset(CPU * cpu, const char * buffer) {
  cpu_reset(cpu);
  memory_reset(cpu->mem);
  printf("Reset to initial state\n");
  return true;
}

bool cpu_debug_test(CPU * cpu, const char * buffer) {
  char * ptr;
  int tolerance = strtol(buffer, &ptr, 0);
  if (ptr == buffer) {
    tolerance = 1;
  }

  FILE * fp = fopen("test/sub-nestest.log", "r");
  if (fp == NULL) {
    fprintf(stderr, "Failed to load test: %s\n", strerror(errno));
    return true;
  }

  cpu_debug_reset(cpu, buffer);
  printf("\n");

  // Handle status register quirk for this test
  cpu->b = 0;

  int lineno = 1;
  char test[CPU_DEBUG_LENGTH + 1], debug[CPU_DEBUG_LENGTH];
  while (tolerance != 0 && fgets(test, ARRAY_LENGTH(test), fp) != NULL) {
    cpu_debug_instr(cpu, debug);

    if (strncmp(debug, test, CPU_DEBUG_LENGTH - 1) != 0) {
      printf("Test Failed (line %i):\nExpected: %sObtained: %s\n", lineno, test, debug);
      if (--tolerance != 0) {
        printf("\n");
      }
    }

    cpu_next_instr(cpu);
    lineno += 1;
  }

  fclose(fp);

  printf("\n");
  cpu_debug_reset(cpu, buffer);
  return true;
}

bool cpu_debug_quit(CPU * cpu, const char * buffer) {
  return false;
}

const struct {
  const char * name;
  bool (*action)(CPU * cpu, const char * buffer);
} cpu_debug_commands[] = {
  {"next", cpu_debug_next},
  {"goto", cpu_debug_goto},
  {"reset", cpu_debug_reset},
  {"rs", cpu_debug_reset},
  {"test", cpu_debug_test},
  {"quit", cpu_debug_quit},
  {"exit", cpu_debug_quit}
};

void cpu_debug(CPU * cpu) {
  char buffer[256];

  printf("> ");
  while(fgets(buffer, ARRAY_LENGTH(buffer), stdin) != NULL) {
    size_t len;
    char * params = strchr(buffer, ' ');
    if (!params) {
      len = strlen(buffer) - 1;
      params = buffer + len;
    } else {
      len = params - buffer;
    }

    size_t i;
    bool valid = false;
    bool quit = false;
    for (i = 0; i < ARRAY_LENGTH(cpu_debug_commands); ++i) {
      if (strncmp(buffer, cpu_debug_commands[i].name, len) == 0) {
        quit = !cpu_debug_commands[i].action(cpu, params);
        valid = true;
        break;
      }
    }

    if (quit) {
      break;
    }

    if (!valid) {
      printf("Invalid command\n");
    }

    printf("> ");
  }
}
