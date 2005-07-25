/* ethernet.h
     $Id$

   written by Marc Singer
   7 Jul 2005

   Copyright (C) 2005 Marc Singer

   -----------
   DESCRIPTION
   -----------


   NOTES
   =====

   ethernet_service() termination functions
   ----------------------------------------

     These functions must return 0 when the is no reason to terminate
     ethernet_service().  By convention, results <0 are errors or
     failures, results >0 are successes.

*/

#if !defined (__ETHERNET_H__)
#    define   __ETHERNET_H__

/* ----- Includes */

#include <driver.h>

/* ----- Types */

#define FRAME_LENGTH_MAX	1536

struct ethernet_frame {
  size_t cb;
  int state;
  char rgb[FRAME_LENGTH_MAX];
};

struct ethernet_timeout_context {
  unsigned long time_start;
  long ms_timeout;
};

typedef int (*pfn_ethernet_receiver) (struct descriptor_d*, 
				      struct ethernet_frame*, void*);

/* ----- Globals */

/* ----- Prototypes */

struct ethernet_frame* ethernet_frame_allocate (void);
void ethernet_frame_release (struct ethernet_frame*);
//void ethernet_receive (struct descriptor_d*, struct ethernet_frame*);
int ethernet_service (struct descriptor_d*, int (*) (void*), void*);

void udp_setup (struct ethernet_frame*, const char*, u16, u16, size_t);

int ethernet_timeout (void*);

int register_ethernet_receiver (int priority, 
				pfn_ethernet_receiver pfn, 
				void* context);
int unregister_ethernet_receiver (pfn_ethernet_receiver pfn, void* context);

/* *** FIXME: perhaps this shouldn't be exported */
void arp_cache_update (const char* hardware_address,
		       const char* protocol_address,
		       int force);

#endif  /* __ETHERNET_H__ */
