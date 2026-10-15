/* Regression tests for the 24-bit bus and tagged effective addresses. */
#include "config.h"
#include "syn68k_public.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main (void)
{
  static const uint16 program[] = {
    0x1010, 0x3210, 0x2410, 0x2282,
    0x45E8, 0x0004, 0x1618, 0x3820,
    0x1A30, 0x7002, 0x2C39, 0xEF00, 0x2000,
    0x47F9, 0xAB00, 0x2000, 0x4E75
  };
  unsigned char *memory = calloc (1, 16 * 1024 * 1024);
  unsigned char native_argument[256] = { 0 };
  uint32 alias;
  unsigned i;
  assert (memory);
  ROMlib_offset = (ptr_sized_uint) memory;
  syn68k_callback_address_space = (uint16 *) (memory + 0x8000);
  syn68k_set_memory_window (16 * 1024 * 1024, 0x400000, 0x400000);
  initialize_68k_emulator (NULL, 1, (uint32 *) memory, 0);
  /* Host pointers published during 32-bit startup must survive a later switch. */
  alias = US_TO_SYN68K (native_argument);
  assert (alias >= 0x400000 && alias < 0x800000);
  syn68k_set_addressing_mode (0);
  assert (SYN68K_TO_US (alias) == (uint16 *) native_argument);
  assert (syn68k_address_mask == 0x00FFFFFF);
  for (i = 0; i < sizeof (program) / sizeof (program[0]); ++i)
    WRITEUW (0x1000 + 2 * i, program[i]);
  WRITEUL (0x2000, 0x12345678);
  WRITEUB (0x1FFF, 0x9A);
  WRITEUL (0x3000, MAGIC_EXIT_EMULATOR_ADDRESS);
  EM_A0 = 0xAB002000;
  EM_A1 = 0xCD002100;
  EM_A7 = 0x3000;
  EM_D7 = 1;
  interpret_code (hash_lookup_code_and_create_if_needed (0xF1001000));
  assert ((EM_D0 & 255) == 0x12);
  assert ((EM_D1 & 65535) == 0x1234);
  assert (EM_D2 == 0x12345678 && READUL (0x2100) == EM_D2);
  assert (EM_A2 == 0xAB002004 && EM_A3 == 0xAB002000);
  assert ((EM_D3 & 255) == 0x12 && (EM_D4 & 65535) == 0x9A12);
  assert ((EM_D5 & 255) == 0x56 && EM_D6 == 0x12345678);
  assert (EM_A0 == 0xAB001FFF);
  assert (SYN68K_TO_US (0xEE002000) == SYN68K_TO_US (0x2000));
  alias = US_TO_SYN68K (native_argument);
  assert (alias >= 0x400000 && alias < 0x800000);
  WRITEUB (alias + 37, 0xA5);
  assert (native_argument[37] == 0xA5);
  assert (SYN68K_TO_US (alias + 37) == (uint16 *) &native_argument[37]);
  syn68k_set_addressing_mode (1);
  assert (syn68k_address_mask == 0xFFFFFFFF);
  assert (SYN68K_TO_US (alias) == (uint16 *) native_argument);
  assert (US_TO_SYN68K (native_argument) == alias);
  syn68k_set_addressing_mode (0);
  WRITEUW (0x15A, 0x0700);
  WRITEUW (0x1100, 0x3E38); /* move.w $015A.w,d7 */
  WRITEUW (0x1102, 0x015A);
  WRITEUW (0x1104, 0x4878); /* pea $015A.w */
  WRITEUW (0x1106, 0x015A);
  WRITEUW (0x1108, 0x205F); /* movea.l (sp)+,a0 */
  WRITEUW (0x110A, 0x4E75);
  EM_A7 = 0x3000;
  WRITEUL (EM_A7, MAGIC_EXIT_EMULATOR_ADDRESS);
  interpret_code (hash_lookup_code_and_create_if_needed (0x1100));
  assert ((EM_D7 & 65535) == 0x0700 && EM_A0 == 0x15A);
  /* The immortal interrupt-return block must also respect the 24-bit bus. */
  EM_A7 = 0x3100;
  WRITEUW (0x3100, 0x2000);
  WRITEUL (0x3102, 0xAB001100);
  WRITEUW (0x3106, 0);
  WRITEUL (0x3108, MAGIC_EXIT_EMULATOR_ADDRESS);
  interpret_code (hash_lookup_code_and_create_if_needed (MAGIC_RTE_ADDRESS));
  assert (EM_A7 == 0x310C && EM_A0 == 0x15A);
  puts ("24-bit CPU addressing tests passed");
  return 0;
}
