#include "cpu_core.hpp"
#include "cpu_cop0.hpp"
#include "cpu_cop2.hpp"
#include "../utility.hpp"

using namespace psxact::cpu;

// --========--
//   Decoding
// --========--

static inline uint32_t overflow(uint32_t x, uint32_t y, uint32_t z) {
  return (~(x ^ y) & (x ^ z) & 0x80000000);
}

uint32_t core::get_register(uint32_t index) {
  if (is_load_delay_slot && load_index == index) {
    return load_value;
  }
  else {
    return regs.gp[index];
  }
}

uint32_t core::get_register_forwarded(uint32_t index) {
  return regs.gp[index];
}

void core::set_rd(uint32_t value) {
  regs.gp[decode_rd()] = value;
  regs.gp[0] = 0;
}

void core::set_rt(uint32_t value) {
  regs.gp[decode_rt()] = value;
  regs.gp[0] = 0;
}

void core::set_rt_load(uint32_t value) {
  uint32_t t = decode_rt();

  if (is_load_delay_slot && load_index == t) {
    regs.gp[t] = load_value;
  }

  is_load = true;
  load_index = t;
  load_value = regs.gp[t];

  regs.gp[t] = value;
  regs.gp[0] = 0;
}

uint32_t core::get_rt() {
  return get_register(decode_rt());
}

uint32_t core::get_rt_forwarded() {
  return get_register_forwarded(decode_rt());
}

uint32_t core::get_rs() {
  return get_register(decode_rs());
}

// --============--
//   Instructions
// --============--

void core::op_add() {
  uint32_t x = get_rs();
  uint32_t y = get_rt();
  uint32_t z = x + y;

  if (overflow(x, y, z)) {
    enter_exception(cop0::exception_code_t::overflow);
  }
  else {
    set_rd(z);
  }
}

void core::op_addi() {
  uint32_t x = get_rs();
  uint32_t y = decode_iconst();
  uint32_t z = x + y;

  if (overflow(x, y, z)) {
    enter_exception(cop0::exception_code_t::overflow);
  }
  else {
    set_rt(z);
  }
}

void core::op_addiu() {
  set_rt(get_rs() + decode_iconst());
}

void core::op_addu() {
  set_rd(get_rs() + get_rt());
}

void core::op_and() {
  set_rd(get_rs() & get_rt());
}

void core::op_andi() {
  set_rt(get_rs() & decode_uconst());
}

void core::op_beq() {
  if (get_rs() == get_rt()) {
    regs.next_pc = regs.pc + (decode_iconst() << 2);
    is_branch = true;
  }
}

void core::op_bgtz() {
  if (int32_t(get_rs()) > 0) {
    regs.next_pc = regs.pc + (decode_iconst() << 2);
    is_branch = true;
  }
}

void core::op_blez() {
  if (int32_t(get_rs()) <= 0) {
    regs.next_pc = regs.pc + (decode_iconst() << 2);
    is_branch = true;
  }
}

void core::op_bne() {
  if (get_rs() != get_rt()) {
    regs.next_pc = regs.pc + (decode_iconst() << 2);
    is_branch = true;
  }
}

void core::op_break() {
  enter_exception(cop0::exception_code_t::breakpoint);
}

void core::op_bxx() {
  // bgez rs,$nnnn
  // bgezal rs,$nnnn
  // bltz rs,$nnnn
  // bltzal rs,$nnnn
  bool condition = (code & (1 << 16))
                   ? int32_t(get_rs()) >= 0
                   : int32_t(get_rs()) < 0;

  if ((code & 0x1e0000) == 0x100000) {
    regs.gp[31] = regs.next_pc;
  }

  if (condition) {
    regs.next_pc = regs.pc + (decode_iconst() << 2);
    is_branch = true;
  }
}

void core::op_cop0() {
  if (code & (1 << 25)) {
    return cop0.run(code & 0x1ffffff);
  }

  uint32_t rd = decode_rd();
  uint32_t rt = decode_rt();

  switch (decode_rs()) {
  case 0x00: return set_rt(cop0.read_gpr(rd));
  case 0x02: return set_rt(cop0.read_gpr(rd));
  case 0x04: return cop0.write_gpr(rd, get_register(rt));
  case 0x06: return cop0.write_ccr(rd, get_register(rt));
  }

  printf("cpu_core::op_cop0(0x%08x)\n", code);
}

void core::op_cop1() {
  enter_exception(cop0::exception_code_t::cop_unusable);
}

void core::op_cop2() {
  if (code & (1 << 25)) {
    return cop2.run(code & 0x1ffffff);
  }

  uint32_t rd = decode_rd();
  uint32_t rt = decode_rt();

  switch (decode_rs()) {
  case 0x00: return set_rt(cop2.read_gpr(rd));
  case 0x02: return set_rt(cop2.read_ccr(rd));
  case 0x04: return cop2.write_gpr(rd, get_register(rt));
  case 0x06: return cop2.write_ccr(rd, get_register(rt));
  }

  printf("cpu_core::op_cop2(0x%08x)\n", code);
}

void core::op_cop3() {
  enter_exception(cop0::exception_code_t::cop_unusable);
}

void core::op_div() {
  int32_t dividend = int32_t(get_rs());
  int32_t divisor = int32_t(get_rt());

  if (dividend == int32_t(0x80000000) && divisor == int32_t(0xffffffff)) {
    regs.lo = 0x80000000;
    regs.hi = 0;
  }
  else if (dividend >= 0 && divisor == 0) {
    regs.lo = uint32_t(0xffffffff);
    regs.hi = uint32_t(dividend);
  }
  else if (dividend <= 0 && divisor == 0) {
    regs.lo = uint32_t(0x00000001);
    regs.hi = uint32_t(dividend);
  }
  else {
    regs.lo = uint32_t(dividend / divisor);
    regs.hi = uint32_t(dividend % divisor);
  }
}

void core::op_divu() {
  uint32_t dividend = get_rs();
  uint32_t divisor = get_rt();

  if (divisor) {
    regs.lo = dividend / divisor;
    regs.hi = dividend % divisor;
  }
  else {
    regs.lo = 0xffffffff;
    regs.hi = dividend;
  }
}

void core::op_j() {
  regs.next_pc = (regs.pc & 0xf0000000) | ((code << 2) & 0x0ffffffc);
  is_branch = true;
}

void core::op_jal() {
  regs.gp[31] = regs.next_pc;
  regs.next_pc = (regs.pc & 0xf0000000) | ((code << 2) & 0x0ffffffc);
  is_branch = true;
}

void core::op_jalr() {
  uint32_t ra = regs.next_pc;

  regs.next_pc = get_rs();
  set_rd(ra);

  is_branch = true;
}

void core::op_jr() {
  regs.next_pc = get_rs();
  is_branch = true;
}

void core::op_lb() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = read_data(BUS_WIDTH_BYTE, address);
  data = utility::sclip<8>(data);

  set_rt_load(data);
}

void core::op_lbu() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = read_data(BUS_WIDTH_BYTE, address);

  set_rt_load(data);
}

void core::op_lh() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 1) {
    enter_exception(cop0::exception_code_t::address_error_load);
  }
  else {
    uint32_t data = read_data(BUS_WIDTH_HALF, address);
    data = utility::sclip<16>(data);

    set_rt_load(data);
  }
}

void core::op_lhu() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 1) {
    enter_exception(cop0::exception_code_t::address_error_load);
  }
  else {
    uint32_t data = read_data(BUS_WIDTH_HALF, address);

    set_rt_load(data);
  }
}

void core::op_lui() {
  set_rt(decode_uconst() << 16);
}

void core::op_lw() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 3) {
    enter_exception(cop0::exception_code_t::address_error_load);
  }
  else {
    uint32_t data = read_data(BUS_WIDTH_WORD, address);

    set_rt_load(data);
  }
}

void core::op_lwc0() {
  printf("cpu_core::op_lwc0(0x%08x)\n", code);
}

void core::op_lwc1() {
  printf("cpu_core::op_lwc1(0x%08x)\n", code);
}

void core::op_lwc2() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 3) {
    enter_exception(cop0::exception_code_t::address_error_load);
  }
  else {
    uint32_t data = read_data(BUS_WIDTH_WORD, address);

    cop2.write_gpr(decode_rt(), data);
  }
}

void core::op_lwc3() {
  printf("cpu_core::op_lwc3(0x%08x)\n", code);
}

void core::op_lwl() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = read_data(BUS_WIDTH_WORD, address & ~3);

  switch (address & 3) {
  default: data = (data << 24) | (get_rt_forwarded() & 0x00ffffff); break;
  case  1: data = (data << 16) | (get_rt_forwarded() & 0x0000ffff); break;
  case  2: data = (data <<  8) | (get_rt_forwarded() & 0x000000ff); break;
  case  3: data = (data <<  0) | (get_rt_forwarded() & 0x00000000); break;
  }

  set_rt_load(data);
}

void core::op_lwr() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = read_data(BUS_WIDTH_WORD, address & ~3);

  switch (address & 3) {
  default: data = (data >>  0) | (get_rt_forwarded() & 0x00000000); break;
  case  1: data = (data >>  8) | (get_rt_forwarded() & 0xff000000); break;
  case  2: data = (data >> 16) | (get_rt_forwarded() & 0xffff0000); break;
  case  3: data = (data >> 24) | (get_rt_forwarded() & 0xffffff00); break;
  }

  set_rt_load(data);
}

void core::op_mfhi() {
  set_rd(regs.hi);
}

void core::op_mflo() {
  set_rd(regs.lo);
}

void core::op_mthi() {
  regs.hi = get_rs();
}

void core::op_mtlo() {
  regs.lo = get_rs();
}

void core::op_mult() {
  int32_t rs = int32_t(get_rs());
  int32_t rt = int32_t(get_rt());

  int64_t result = int64_t(rs) * int64_t(rt);
  regs.lo = uint32_t(result >> 0);
  regs.hi = uint32_t(result >> 32);
}

void core::op_multu() {
  uint32_t s = get_rs();
  uint32_t t = get_rt();

  uint64_t result = uint64_t(s) * uint64_t(t);
  regs.lo = uint32_t(result >> 0);
  regs.hi = uint32_t(result >> 32);
}

void core::op_nor() {
  set_rd(~(get_rs() | get_rt()));
}

void core::op_or() {
  set_rd(get_rs() | get_rt());
}

void core::op_ori() {
  set_rt(get_rs() | decode_uconst());
}

void core::op_sb() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = get_rt();

  write_data(BUS_WIDTH_BYTE, address, data);
}

void core::op_sh() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 1) {
    enter_exception(cop0::exception_code_t::address_error_store);
  }
  else {
    uint32_t data = get_rt();

    write_data(BUS_WIDTH_HALF, address, data);
  }
}

void core::op_sll() {
  set_rd(get_rt() << decode_sa());
}

void core::op_sllv() {
  set_rd(get_rt() << get_rs());
}

void core::op_slt() {
  set_rd(int32_t(get_rs()) < int32_t(get_rt()) ? 1 : 0);
}

void core::op_slti() {
  set_rt(int32_t(get_rs()) < int32_t(decode_iconst()) ? 1 : 0);
}

void core::op_sltiu() {
  set_rt(get_rs() < decode_iconst() ? 1 : 0);
}

void core::op_sltu() {
  set_rd(get_rs() < get_rt() ? 1 : 0);
}

void core::op_sra() {
  set_rd(int32_t(get_rt()) >> decode_sa());
}

void core::op_srav() {
  set_rd(int32_t(get_rt()) >> get_rs());
}

void core::op_srl() {
  set_rd(get_rt() >> decode_sa());
}

void core::op_srlv() {
  set_rd(get_rt() >> get_rs());
}

void core::op_sub() {
  uint32_t x = get_rs();
  uint32_t y = get_rt();
  uint32_t z = x - y;

  if (overflow(x, ~y, z)) {
    enter_exception(cop0::exception_code_t::overflow);
  }
  else {
    set_rd(z);
  }
}

void core::op_subu() {
  set_rd(get_rs() - get_rt());
}

void core::op_sw() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 3) {
    enter_exception(cop0::exception_code_t::address_error_store);
  }
  else {
    uint32_t data = get_rt();

    write_data(BUS_WIDTH_WORD, address, data);
  }
}

void core::op_swc0() {
  printf("cpu_core::op_swc0(0x%08x)\n", code);
}

void core::op_swc1() {
  printf("cpu_core::op_swc1(0x%08x)\n", code);
}

void core::op_swc2() {
  uint32_t address = get_rs() + decode_iconst();
  if (address & 3) {
    enter_exception(cop0::exception_code_t::address_error_store);
  }
  else {
    uint32_t data = cop2.read_gpr(decode_rt());

    write_data(BUS_WIDTH_WORD, address, data);
  }
}

void core::op_swc3() {
  printf("cpu_core::op_swc3(0x%08x)\n", code);
}

void core::op_swl() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = read_data(BUS_WIDTH_WORD, address & ~3);

  switch (address & 3) {
  default: data = (data & 0xffffff00) | (get_rt() >> 24); break;
  case  1: data = (data & 0xffff0000) | (get_rt() >> 16); break;
  case  2: data = (data & 0xff000000) | (get_rt() >>  8); break;
  case  3: data = (data & 0x00000000) | (get_rt() >>  0); break;
  }

  write_data(BUS_WIDTH_WORD, address & ~3, data);
}

void core::op_swr() {
  uint32_t address = get_rs() + decode_iconst();
  uint32_t data = read_data(BUS_WIDTH_WORD, address & ~3);

  switch (address & 3) {
  default: data = (data & 0x00000000) | (get_rt() <<  0); break;
  case  1: data = (data & 0x000000ff) | (get_rt() <<  8); break;
  case  2: data = (data & 0x0000ffff) | (get_rt() << 16); break;
  case  3: data = (data & 0x00ffffff) | (get_rt() << 24); break;
  }

  write_data(BUS_WIDTH_WORD, address & ~3, data);
}

void core::op_syscall() {
  enter_exception(cop0::exception_code_t::syscall);
}

void core::op_xor() {
  set_rd(get_rs() ^ get_rt());
}

void core::op_xori() {
  set_rt(get_rs() ^ decode_uconst());
}

void core::op_und() {
  enter_exception(cop0::exception_code_t::reserved_instruction);
}
