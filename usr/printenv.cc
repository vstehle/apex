/* printenv.cc

   written by Marc Singer
   23 Mar 2006

   Copyright (C) 2006 Marc Singer

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

   Program to read the APEX environment from user-land.  This is a
   very crude implementation.  It doesn't check that the environment's
   magic number matches APEX's expected magic number.  If APEX ever
   changes to a general purpose environment, where variables can be
   added by the user, the ENV_LINK_MAGIC number will change and this
   code will have to adapt.

   This implementation isn't terribly robust.  It doesn't parse the
   environment region very carefully.  It really only works when the
   region is of the form "nor:start+length" where start and length of
   decimal integers optinally followed by 'k' for a 1024 multiplier.
   Generally, this will work, but it ought to allow for hexadecimal
   values as well.

   This implementation is smart enough to detect the new environment
   format where it is possible to read the environment keys without
   scanning through the APEX binary.  It still isn't possible to know
   the default environment values without reading APEX, so this code
   will still probe into the APEX binary.

   There ought to be a switch that allows the user to read the
   environment region without reading APEX, thus only seeing the
   values that are modified and stored in the environment.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/io.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#include "environment.h"

#define DEVICE "/dev/mtdblock0"

#define CB_ENV_MAGIC	(2)

typedef unsigned short u16;
typedef unsigned long u32;

struct descriptor {
  char driver[80];
  unsigned long start;
  unsigned long length;
};

struct entry {
  int index;			// index of this variable in APEX or 0x7f
  char* key;
  char* value;
};

void dumpw (const void* pv, int cb, unsigned long index, int width)
{
  const unsigned char* rgb = (const unsigned char*) pv;
  int i;

  while (cb > 0) {
    printf ("%08lx: ", index);
    for (i = 0; i < 16; ++i) {
      if (i < cb) {
	switch (width) {
	default:
	case 1:
	  printf ("%02x ", rgb[i]);
	  break;
	case 2:
	  printf ("%04x ", *((u16*)&rgb[i]));
	  ++i;
	  break;
	case 4:
	  printf ("%08x ", *((u32*)&rgb[i]));
	  i += 3;
	  break;
	}
      }
      else
	printf ("   ");
      if (i%8 == 7)
	putchar (' ');
    }
    for (i = 0; i < 16; ++i) {
      if (i == 8)
	putchar (' ');
      putchar ( (i < cb) ? (isprint (rgb[i]) ? rgb[i] : '.') : ' ');
    }
    printf ("\n");

    cb -= 16;
    index += 16;
    rgb += 16;
  }
}

void copy_string (void* pv, const struct env_link& env_link, char** szCopy)
{
  char* szSource = (char*) pv + (*szCopy - (char*) env_link.apex_start);
  int cb = strlen (szSource);
  char* sz = new char[cb + 1];
  strcpy (sz, szSource);
  *szCopy = sz;
}


/* parse_region

   performs a simplified parse of a region descriptor
*/

struct descriptor parse_region (const char* sz)
{
  struct descriptor d;
  memset (&d, 0, sizeof (d));

  char* pch;
  if ((pch = index (sz, ':'))) {
    int c = pch - sz;
    if (c > sizeof (d.driver) - 1)
      c = sizeof (d.driver) - 1;
    memcpy (d.driver, sz, c);
    d.driver[c] = 0;
    sz = pch + 1;
  }
  d.start = strtoul (sz, (char**) &sz, 10);
  if (*sz == 'k' || *sz == 'K') {
    ++sz;
    d.start *= 1024;
  }
  if (*sz == '+') {
    d.length = strtoul (sz, (char**) &sz, 10);
    if (*sz == 'k' || *sz == 'K') {
      ++sz;
      d.length *= 1024;
    }
  }
  return d;
}


/* scan_environment

   is the guts of this program.  It scans the environment,
   constructing a map of the environment variables that it finds.
   These will be all of the entries that exist in non-volatile memory,
   but these may not all have corresponding entries among APEXs
   environment descriptors.  If it is given an env_d it will fill in
   the index field for the entries.

   The return value is the count of unique ids found, or -1 if the is
   environment is unrecognized.

*/

int scan_environment (struct env_d* env, int c_env, void* pv,
		      struct entry* rgEntry)
{
  for (int i = 0; i < sizeof (rgEntry)/sizeof (rgEntry[0]); ++i)
    rgEntry[i].index = 0xff;

  unsigned char* pb = (unsigned char*) pv;

  if (pb[0] == 0xff && pb[1] == 0xff)
    return 0;

  if (pb[0] != ENV_MAGIC_0 || pb[1] != ENV_MAGIC_1)
    return -1;

  if (c_env <= 0 || env == NULL)
    c_env = 0;

  int c = 0;

  while (*pb != 0xff) {
    unsigned char flags = *pb++;
    int id = flags & 0x7f;
    if (rgEntry[id].index == 0xff) {
      int cb = strlen ((char*) pb);
      rgEntry[id].key = new char [cb + 1];
      strcpy (rgEntry[id].key, (char*) pb);
      pb += cb + 1;
      rgEntry[id].index = 0x7f;
      for (int index = 0; index < c_env; ++index)
	if (strcasecmp (rgEntry[id].key, env[index].key) == 0) {
	  rgEntry[id].index = index;
	  break;
	}
      ++c;
    }
    int cb = strlen ((char*) pb);
    if (flags & 0x80) {
      rgEntry[id].value = new char [cb + 1];
      strcpy (rgEntry[id].value, (char*) pb);
    }
    pb += cb + 1;
  }
  return c;
}


/* show_environment

   knits together the two different kinds of environment data and
   displays the values.

*/

void show_environment (struct env_d* env, int c_env,
		       struct entry* rgEntry, int c_entry)
{
  char rgId[127];
  memset (rgId, 0xff, sizeof (rgId));

  //  dumpw (pv, 128, 0, 1);

  for (int i = 0; i < c_env; ++i) {
    char* value = NULL;
    for (int j = 0; j < c_entry; ++j) {
      if (rgEntry[j].index == i) {
	value = rgEntry[j].value;
	break;
      }

    if (value)
      printf ("%s = %s\n", env[i].key, value);
    else
      printf ("%s *= %s\n", env[i].key, env[i].default_value);
    }
  }
  for (int j = 0; j < c_entry; ++j) {
    if (rgEntry[j].index == 0x7f)
      printf ("%s #= %s\n", rgEntry[j].key, rgEntry[j].value);
  }
}

int main (int argc, char** argv)
{
  int fh = open (DEVICE, O_RDONLY);

  if (fh == -1) {
    printf ("unable to open %s\n", DEVICE);
    return 1;
  }

  void* pv = mmap (NULL, 1024, PROT_READ, MAP_SHARED, fh, 0);
  if (pv == MAP_FAILED) {
    printf ("unable to mmap\n");
    return 1;
  }

  int index_env_link = 0;
  size_t cbApex = 0;

  {
    unsigned long* rgl = (unsigned long*) pv;
    for (int i = 0;
	 i < 1024/sizeof (unsigned long)
	   - sizeof (env_link)/sizeof (unsigned long);
	 ++i) {
      if (rgl[i] == ENV_LINK_MAGIC) {
	struct env_link* env_link = (struct env_link*) &rgl[i];
	cbApex = (char*) env_link->apex_end - (char*) env_link->apex_start;
	index_env_link = i*sizeof (unsigned long);
	break;
      }
    }
  }

  if (!index_env_link) {
    printf ("no env_link\n");
    return 1;
  }

  munmap (pv, 1024);

  pv = mmap (NULL, cbApex, PROT_READ, MAP_PRIVATE, fh, 0);
  if (pv == NULL) {
    printf ("unable to mmap\n");
  }

  const struct env_link& env_link
    = *(const struct env_link*) ((unsigned char*) pv + index_env_link);
  int c_env = ((char*) env_link.env_end - (char*) env_link.env_start)
    /env_link.env_d_size;
  void* pvEnv = NULL;

  struct descriptor d = parse_region (env_link.region);
  if (d.start && d.length) {
    pvEnv = mmap (NULL, d.length, PROT_READ, MAP_PRIVATE, fh, d.start);
    if (pvEnv == MAP_FAILED) {
      printf ("unable to mmap environment");
      return 1;
    }
  }

//  printf ("environment at 0x%x+0x%x -> %p\n", d.start, d.length, pvEnv);

//  printf ("link 0x%x %d %s\n",
//	  (char*) env_link.env_start - (char*) env_link.apex_start,
//	  c_env, env_link.region);

	// --- Pull the environment descriptors from APEX

  struct env_d* env = new env_d[c_env];
  memcpy (env, (const char*) pv
	  + ((char *) env_link.env_start - (char*) env_link.apex_start),
	  c_env*sizeof (struct env_d));
  for (int i = 0; i < c_env; ++i) {
    copy_string (pv, env_link, &env[i].key);
    copy_string (pv, env_link, &env[i].default_value);
    copy_string (pv, env_link, &env[i].description);
//    printf ("%s *= %s\n", env[i].key, env[i].default_value);
  }

  struct entry rgEntry[127];
  int c_entry = scan_environment (env, c_env, env, rgEntry);
  show_environment (env, c_env, rgEntry, c_entry);

  munmap (pv, cbApex);
  munmap (pvEnv, d.length);
  close (fh);

  return 0;
}
