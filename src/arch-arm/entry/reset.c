/* reset.c

   written by Marc Singer
   28 October 2004

   Copyright (C) 2004 Marc Singer

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
   USA.

   -----------
   DESCRIPTION
   -----------

   True, default entry for APEX loader on ARM.  The real entry point
   is entry () which is linked in ../lib/entry.c.  Some of the symbols
   used herein may be overridden by target specific implementations.
   Refer to the documentation for details.

   preinitialization and reset_finish
   ----------------------------------

   In order to support the NAND boot on the lh7 processors where the
   processor may only load 512 bytes from flash, we support a
   preinitialization() call to move more data from flash to memory
   before initialization the SDRAM.  In order to maximize the size of
   the preinitialization function, we put it in it's own segment,
   immediately following .reset, and followed by the reset of the
   bootstrap segment.  The reset_finish() function handles the rest of
   the reset-made calls.

*/

#include <config.h>
#include <asm/bootstrap.h>
#include <debug_ll.h>
#include <arch-arm.h>
#include <memtest.h>
#include <asm/cp15.h>

extern void reset (void);
extern int  initialize_bootstrap (void);
extern void setup_c (void);
extern void init (void);
extern void reset_finish (void);


/* entry

   the first code executed in APEX.  We may or may not have an
   environment link or functions in the top of the loader.  This jump
   to the reset handler allows us to put anything we need, here at the
   top of APEX and skip it straight away.  While this implementation
   is specific to ARM, other architectures will likely have, at a
   minimum, the environment link structure.

*/

void __naked __section (.entry) entry (void)
{
#if defined (CONFIG_ENV_LINK)
  /* Presently, we only need to skip something if there is an
     environment link. */
  __asm volatile ("b reset\n\t");
#endif
}

/* reset

   implements the reset exception vector.  All before init() MUST NOT
   DEPEND on a stack or any RAM whatsoever.

   Also note that nothing in this function can allocate data in the
   code section since there is no jump at the end of the function to
   skip it.

*/

void __naked __section (.reset) reset (void)
{
#if defined (CONFIG_BIGENDIAN)
  {
    unsigned long v;
    __asm volatile ("mrc p15, 0, %0, c1, c0, 0\n\t"
		    "orr %0, %0, #(1<<7)\n\t"	      /* Switch to bigendian */
		    "mcr p15, 0, %0, c1, c0, 0" : "=&r" (v));
    CP15_WAIT;
  }
#endif

#if defined (CONFIG_LITTLEENDIAN)
  {
    unsigned long v;
    __asm volatile ("mrc p15, 0, %0, c1, c0, 0\n\t"
		    "bic %0, %0, #(1<<7)\n\t"	   /* Switch to littleendian */
		    "mcr p15, 0, %0, c1, c0, 0" : "=&r" (v));
    CP15_WAIT;
    /* *** FIXME: the redboot code performed a read from the ttb
       register as a delay.  Not sure why. */
  }
#endif

  /* We coerce the system into supervisor mode and disable interrupts
     in the first few instructions.  The CPU comes out of reset with
     interrupts enabled, but in an undefined operating mode.  Should
     we be starting in a mode that cannot reload CPSR, this won't do
     anything and we won't be able to execute anything. */
  {
    unsigned long l;
    __asm volatile ("mov %0, #0xd3\n\t"
		    "msr cpsr_c, %0" : "=&r" (l));
  }

#if defined (CONFIG_DISABLE_MMU_AT_BOOT)

  UNLOCK_CACHE;

  /* This disables the MMU, though there should be no reason to do so.
     Still, there are some instances where it is necessary because: a
     preexisting bootloader fails to disable the MMU before calling
     other programs, the OS jumps to the bootloader to restart the
     machine, something else is screwy.  If the MMU mappings are
     wonky, disabling the MMU may have dire consequences.

     This is really much worse than that.  In the case of a system
     that doesn't disable the MMU, we need to perform a batch of CP15
     instructions to make sure the cache is correctly flushed.  Just
     disabling the MMU isn't going to be enough.  This will have to be
     linked in with the mmu code so that everything necessary is done.

     It's also not clear how much of this code needs to be
     architecture specific.

     The ARMv6 implementation of this function has the MMU disable
     after the flushes/invalidates.  I think that the ARMv4 (or
     Xscale) versions may put the disable first.

  */

  CLEANALL_DCACHE;
  INVALIDATE_ICACHE;
  INVALIDATE_DCACHE;
  INVALIDATE_TLB;
  DRAIN_WRITE_BUFFER;
  CP15_WAIT;
  __asm volatile ("mcr p15, 0, %0, c2, c0" : : "r" (0)); /* Clear ttbl */

  {
    unsigned long l;
    __asm volatile ("mrc p15, 0, %0, c1, c0, 0\n\t"
		    "bic %0, %0, #(1<<0)\n\t"		/* MMU enable */
		    "bic %0, %0, #(1<<1)\n\t"		/* Alignment */
		    "bic %0, %0, #(1<<2)\n\t"		/* DCache */
		    "bic %0, %0, #(1<<12)\n\t"		/* ICache */
		    "mcr p15, 0, %0, c1, c0, 0\n\t"
		    "nop\n\t"
		    "nop\n\t"
		    "nop\n\t"
		    : "=&r" (l)
		    );
    CP15_WAIT;
  }

#endif

  /* *** This fragment exists to help locate problems with code
     jumping into the wrong place and other bothersome errors.  In
     general, the loader should be free of alignment issues, so we
     don't need to check for alignment errors.  */
#if defined (CONFIG_ALIGNMENT_TRAP) && 0
  {
    unsigned long l;
    __asm volatile ("mrc p15, 0, %0, c1, c0, 0\n\t"
		    "orr %0, %0, #(1<<1)\n\t" /* Enable alignment trap */
		    "mcr p15, 0, %0, c1, c0, 0\n\t" : "=&r" (l)
		    );
  }
  CP15_WAIT;
#endif

#if defined (CONFIG_PREINITIALIZATION)
  /* *** FIXME: legacy implementation. */
  __asm volatile ("bl preinitialization"); /* Avoid tail call optimization */
#endif
}

void __naked __section (.postinitialization) reset_finish_0 (void)
{
  /* *** FIXME: we'd rather not make a call here.  Instead, we'd like
     *** to be able to use a flow of code to get to this point.
     *** Functions and the like in the platform initialization are not
     *** our concern.  The only thing that should be true is that the
     *** return value from the initialization is placed in r0 and we
     *** test it here to determine whether or not we can/should
     *** perform the memory test. */

  /* The initialize_bootstrap () function *must* return TRUE when it
     initialized SDRAM; otherwise, we may clobber ourself in the
     memory test. */

  if (initialize_bootstrap ())	/* Initialization critical to relocate */
#if !defined (CONFIG_BOOTSTRAP_MEMTEST)
  ;
#else
  {
    unsigned long result = 0;

    PUTC ('M');

    result = memory_test_0 (CONFIG_BOOTSTRAP_MEMTEST_BASE,
			    CONFIG_BOOTSTRAP_MEMTEST_SIZE);
    if (result) {
      PUTC ('!');
      PUTHEX (result);
      __asm volatile ("0: b 0b");
    }
    PUTC ('m');
  }
#endif

  PUTC_LL ('E');
  PUTC_LL ('r');
  relocate_apex ();
  __asm volatile ("b reset_finish_1");
}

void __naked __section (.postinitialization) reset_finish_1 (void)
{
  PUTC_LL ('C');
  setup_c ();			/* Setups before executing C code */

  PUTC_LL ('c');

	/* Start loader proper which doesn't return */
  __asm volatile ("b init");
}


/* setup_c

   performs setup necessary to make standard C (APCS) happy, a stack,
   a clear BSS, and data variables in RAM.  The later is usually
   handled by the relocate.

*/

void __naked setup_c (void)
{
	/* Setup stack, quite trivial */
  __asm volatile ("mov sp, %0" :: "r" (&APEX_VMA_STACK_START));

#if defined (CONFIG_CLEAR_STACKS)
	/* Clear stacks */
 {
   void* p = &APEX_VMA_STACKS_START;
   __asm volatile (
		   "0: stmia %0!, {%2}\n\t"
		   "   cmp %0, %1\n\t"
		   "   bls 0b\n\t"
		   : "+r" (p)
		   :  "r" (&APEX_VMA_STACKS_END), "r" (0xe5e5e5e5));
 }
#endif

	/* Clear BSS */
 {
   void* p = &APEX_VMA_BSS_START;
   __asm volatile (
		   "0: cmp %0, %1\n\t"
		      "stmlsia %0!, {%2}\n\t"
		      "bls 0b\n\t"
		   : "+r" (p)
		   :  "r" (&APEX_VMA_BSS_END), "r" (0));
 }

  __asm volatile ("mov pc, lr");
}

void __div0 (void)
{
  /* *** FIXME: perhaps we should reset */

  while (1)
    ;
}
