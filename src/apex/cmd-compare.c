/* cmd_compare.c
     $Id$

   written by Marc Singer
   5 Feb 2005

   Copyright (C) 2005 Marc Singer

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

*/

#include <config.h>
#include <linux/types.h>
#include <apex.h>
#include <command.h>
#include <driver.h>
#include <error.h>
#include <linux/kernel.h>
#include <linux/string.h>


/* cmd_compare

   compares two regions.  If the regions are not identical, it will
   display the offset of the first different byte.

*/

int cmd_compare (int argc, const char** argv)
{
  struct descriptor_d din;
  struct descriptor_d dout;
  int result = 0;
  ssize_t cbCompare = 0;
  size_t cbTotal = 0;
  int count = 1;

  if (argc >= 3 && strcmp (argv[1], "-c") == 0) {
    count = simple_strtoul (argv[2], NULL, 0);
    argc -= 2;
    argv += 2;
  }

  if (count <= 0)
    ERROR_RETURN (ERROR_PARAM, "count must be >0");

  if (argc < 3)
    return ERROR_PARAM;

  if (   (result = parse_descriptor (argv[1], &din))
      || (result = open_descriptor (&din))) {
    printf ("Unable to open target %s\n", argv[1]);
    goto fail_early;
  }

  if (   (result = parse_descriptor (argv[2], &dout))
      || (result = open_descriptor (&din))) {
    printf ("Unable to open target %s\n", argv[2]);
    goto fail;
  }

  if (!din.driver->read || !dout.driver->read) {
    result = ERROR_UNSUPPORTED;
    goto fail;
  }

  /* Make lengths the same */
  cbTotal = din.length;
  if (dout.length && dout.length < din.length)
    cbTotal = dout.length;
  din.length = cbTotal;
  dout.length = cbTotal;

  while (cbCompare < cbTotal && count) {
    char rgbIn [1024];
    char rgbOut[1024];
    ssize_t cbIn;
    ssize_t cbOut;
    ssize_t cb;

    cbIn  = din.driver ->read (&din,  rgbIn,  sizeof (rgbIn));
    cbOut = dout.driver->read (&dout, rgbOut, sizeof (rgbOut));

    if (cbIn != cbOut) {
      printf ("\rregions not the same length\n");
      result = ERROR_FALSE;
      goto fail;
    }

    for (cb = 0; cb < cbIn; ++cb) {
      if (rgbIn[cb] != rgbOut[cb]) {
	printf ("\rregions differ 0x%02x != 0x%02x at %d (0x%x)\n",
		rgbIn[cb], rgbOut[cb], cb + cbCompare, cb + cbCompare);
	result = ERROR_FALSE;
	if (--count == 0)
	  goto fail;
      }
    }
    cbCompare += cbIn;
  }

  if (result == 0)
    printf ("\r%d bytes the same\n", cbCompare);

 fail:
  close_descriptor (&din);
  close_descriptor (&dout);
 fail_early:

  return result;
}

static __command struct command_d c_compare = {
  .command = "compare",
  .description = "compare regions",
  .func = cmd_compare,
COMMAND_HELP(
"compare [-c COUNT] REGION1 REGION2\n"
"  Compare regions and report the differences.\n"
"  The optional COUNT argument specifies a maxmimum number of\n"
"  differences to report.  The default is 1.\n"
)
};
