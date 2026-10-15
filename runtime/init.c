#include "config.h"

#include "syn68k_private.h"
#include "hash.h"
#include "rangetree.h"
#include "callback.h"
#include "trap.h"
#include "alloc.h"
#include "mapping.h"
#include "translate.h"
#include "native.h"
#include "checksum.h"
#include "deathqueue.h"
#include "interrupt.h"
#include "destroyblock.h"
#ifdef USE_BIOS_TIMER
#include "go32.h"
#include "dpmi.h"
#endif   /* USE_BIOS_TIMER */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


/* Global CPU state struct. */
CPUState cpu_state;

#if SIZEOF_CHAR_P == 4
uint32 ROMlib_offset;
#else
uint64 ROMlib_offset;
#endif

uint32 syn68k_address_mask = 0xFFFFFFFFUL;
unsigned syn68k_host_alias_count;
uint32 syn68k_host_alias_base, syn68k_host_alias_size;
static uint32 direct_memory_size;
static ptr_sized_uint host_alias[64];
static int requested_native_code_p;

#define HOST_ALIAS_SHIFT 17
#define HOST_ALIAS_BYTES (1UL << HOST_ALIAS_SHIFT)

void
syn68k_set_memory_window (uint32 size, uint32 base, uint32 alias_size)
{
  direct_memory_size = size;
  syn68k_host_alias_base = base;
  syn68k_host_alias_size = alias_size;
  syn68k_host_alias_count = 0;
}

void *
syn68k_guest_to_host (uint32 address)
{
  uint32 relative = address - syn68k_host_alias_base;
  unsigned index = relative >> HOST_ALIAS_SHIFT;
  if (index < syn68k_host_alias_count)
    return (void *) (host_alias[index] + (relative & (HOST_ALIAS_BYTES - 1)));
  return SYN68K_TO_US_RAW (address);
}

uint32
syn68k_host_to_guest (ptr_sized_uint address)
{
  ptr_sized_uint base = address & ~(ptr_sized_uint)(HOST_ALIAS_BYTES - 1);
  unsigned i;
  if (!address)
    return 0;
  if (address >= ROMlib_offset
      && address - ROMlib_offset < direct_memory_size)
    return US_TO_SYN68K_RAW (address);
  for (i = 0; i < syn68k_host_alias_count; ++i)
    if (host_alias[i] == base)
      return syn68k_host_alias_base + i * HOST_ALIAS_BYTES + address - base;
  if (!syn68k_host_alias_size && syn68k_address_mask == 0xFFFFFFFFUL)
    return US_TO_SYN68K_RAW (address);
  if (syn68k_host_alias_count >= sizeof (host_alias) / sizeof (host_alias[0])
      || (syn68k_host_alias_count + 1) * HOST_ALIAS_BYTES > syn68k_host_alias_size)
    {
      fprintf (stderr, "syn68k: exhausted 24-bit host pointer mappings\n");
      abort ();
    }
  host_alias[syn68k_host_alias_count++] = base;
  return syn68k_host_alias_base + i * HOST_ALIAS_BYTES + address - base;
}

void
syn68k_set_addressing_mode (int mode32)
{
  uint32 mask = mode32 ? 0xFFFFFFFFUL : 0x00FFFFFFUL;
  if (mask == syn68k_address_mask)
    return;
  fprintf (stderr, "trace: MMU switch %d -> %d aliases=%u\n",
           syn68k_address_mask == 0xFFFFFFFFUL ? 32 : 24,
           mode32 ? 32 : 24, syn68k_host_alias_count);
  destroy_blocks (0, ~0UL);
  syn68k_address_mask = mask;
#ifdef GENERATE_NATIVE_CODE
  native_code_p = mode32 && !syn68k_host_alias_size && requested_native_code_p;
#endif
  memset (cpu_state.jsr_stack, -1, sizeof (cpu_state.jsr_stack));
  cpu_state.jsr_stack_byte_index = 0;
}

/* This function initializes syn68k.  Call it exactly once, before any
 * other syn68k calls.  DOS_INT_FLAG_ADDR is the conventional memory
 * offset of the 32 bit synchronous interrupt flag.  Just pass in zero
 * for the non-MSDOS and non-SYNCHRONOUS_INTERRUPTS cases.
 */
void
initialize_68k_emulator (void (*while_busy)(int), int native_p,
			 uint32 trap_vector_storage[64],
			 uint32 dos_int_flag_addr)
{
  static uint16 rte =
#ifdef LITTLEENDIAN
    0x734E;
#else
    0x4E73;
#endif
  Block *b;
  uint16 *code;

#ifdef __CHECKER__
  /* Checker can't work with native code. */
  native_p = FALSE;
#endif

  /* Zero out CPU state (not necessary since statics are 0'd) */
  memset (&cpu_state, 0, sizeof cpu_state);
  memset (&cpu_state.jsr_stack, -1, sizeof cpu_state.jsr_stack);

  /* Record the function to periodically call while we are busy doing stuff. */
  call_while_busy_func = while_busy;

#ifdef GENERATE_NATIVE_CODE
  native_code_p = native_p && !syn68k_host_alias_size;
  requested_native_code_p = native_p;
#endif
  trap_vector_array = trap_vector_storage;

  /* Set up the status register & interrupt stack pointer. */
  cpu_state.sr = 0x2000;   /* supervisor/interrupt mode (this appears
			    * to be what programs really expect, e.g.
			    * PGA Tour Golf does movew #8192,sr).
			    */
  cpu_state.usp = 0xDEADF00D;  /* Never use this stack pointer! */
  cpu_state.msp = 0xDEAD0666;  /* Never use this stack pointer! */

#ifdef USE_BIOS_TIMER
  dos_memory_selector = _go32_conventional_mem_selector ();
  assert (dos_int_flag_addr != 0);
  dos_interrupt_flag_addr = dos_int_flag_addr;
#endif  /* USE_BIOS_TIMER */
  
#ifdef SYNCHRONOUS_INTERRUPTS
  SET_INTERRUPT_STATUS (INTERRUPT_STATUS_UNCHANGED);
#endif

  /* Call various initialization routines. */
  hash_init ();
  range_tree_init ();
  callback_init ();
  trap_init ();

#ifdef GENERATE_NATIVE_CODE
  native_code_init ();
#endif  /* GENERATE_NATIVE_CODE */

  /* Create the magical block that exits the emulator if you ever
   * jump to it.  Don't add it to the death queue, since we never want
   * this block to go away.
   */
  b = make_artificial_block (NULL, MAGIC_EXIT_EMULATOR_ADDRESS,
			     OPCODE_WORDS, &code);

#ifdef USE_DIRECT_DISPATCH
  *(const void **)code = direct_dispatch_table[0];
#else
  *(const void **)code = (const void *)0; /* Exit emulator opc. */
#endif

  hash_insert (b);
  range_tree_insert (b);

  /* Create the magical block that contains an RTE. */
  b = NULL;
#ifdef GENERATE_NATIVE_CODE
  native_code_p = FALSE;
#endif
#if SIZEOF_CHAR_P != 8
  generate_block (NULL, US_TO_SYN68K (&rte), &b, TRUE);
#else
  {
    typeof (ROMlib_offset) save_offset;

    save_offset = ROMlib_offset;
    ROMlib_offset = &rte; /* so the RTE will be reachable with 32 bit addr */
    generate_block (NULL, US_TO_SYN68K (&rte), &b, TRUE);
    ROMlib_offset = save_offset;
  }
#endif
#ifdef GENERATE_NATIVE_CODE
  native_code_p = requested_native_code_p && !syn68k_host_alias_size;
#endif
  assert (b != NULL);
  hash_remove (b);
  range_tree_remove (b);
  death_queue_dequeue (b);
  b->m68k_start_address = MAGIC_RTE_ADDRESS;
  b->m68k_code_length   = 1;
  b->checksum = compute_block_checksum (b);
  b->immortal = TRUE;
  hash_insert (b);
  range_tree_insert (b);
}
