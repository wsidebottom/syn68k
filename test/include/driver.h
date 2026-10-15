#ifndef _driver_h_
#define _driver_h_

#include "syn68k_public.h"

typedef enum {
  DM_USE_EMULATOR,
  DM_USE_68000,
  DM_COMPARE_AS_YOU_GO
  } DriverMode;

extern uint32 times_called;
extern uint16 ccr;
extern int test_only_non_cc_variants;
extern int generate_crc;

extern void test_all_instructions (uint32 try_count);

#define MAX_ERRORS 30

#endif  /* Not driver_h_ */
/* Reserve the reference address before native-code allocation on Windows. */
#if defined (__MINGW32__)
extern void reserve_reference_test_memory (void);
#endif
