/* $Id$
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * User Mode IDI Interface
 *
 * Copyright 2000,2001 by Armin Schindler (mac@melware.de)
 * Copyright 2000,2001 Cytronics & Melware (info@melware.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "platform.h"
#include "dqueue.h"

static spinlock_t dqueue_lock;

int
diva_data_q_init(diva_um_idi_data_queue_t* q,
                 int max_length,
                 int max_segments)
{
  int i;

  dqueue_lock = SPIN_LOCK_UNLOCKED;

  q->max_length	= max_length;
  q->segments = max_segments;
	
  for (i = 0; i < q->segments; i++) {
    q->data[i] = 0;
    q->length[i]  = 0;
  }
  q->read = q->write = q->count = q->segment_pending = 0;

  for (i = 0; i < q->segments; i++) {
    if (!(q->data[i] = diva_os_malloc (0, q->max_length))) {
      diva_data_q_finit (q);
      return (-1);
    }
  }

  return (0);
}

int
diva_data_q_finit(diva_um_idi_data_queue_t* q)
{
  int i;

  for (i = 0; i < q->segments; i++) {
    if (q->data[i]) {
      diva_os_free (0, q->data[i]);
    }
    q->data[i] = 0;
    q->length[i] = 0;
  }
  q->read = q->write = q->count = q->segment_pending = 0;

  return (0);
}

int
diva_data_q_get_max_length(const diva_um_idi_data_queue_t* q)
{
  return (q->max_length);
}

void*
diva_data_q_get_segment4write(diva_um_idi_data_queue_t* q)
{
  void *ret = NULL;

  spin_lock(&dqueue_lock);
  if ((!q->segment_pending) && (q->count < q->segments)) {
    q->segment_pending = 1;
    ret = q->data[q->write];
  }
  spin_unlock(&dqueue_lock);

  return (ret);
}

void
diva_data_q_ack_segment4write(diva_um_idi_data_queue_t* q, int length)
{
  spin_lock(&dqueue_lock);
  if (q->segment_pending) {
    q->length[q->write] = length;
    q->count++;
    q->write++;
    if (q->write >= q->segments) {
      q->write = 0;
    }
    q->segment_pending = 0;
  }
  spin_unlock(&dqueue_lock);
}

const void*
diva_data_q_get_segment4read(const diva_um_idi_data_queue_t* q)
{
  void *ret = NULL;

  spin_lock(&dqueue_lock);
  if (q->count) {
    ret = q->data[q->read];
  }
  spin_unlock(&dqueue_lock);
  return (ret);
}

int
diva_data_q_get_segment_length(const diva_um_idi_data_queue_t* q)
{
  int ret;
  
  spin_lock(&dqueue_lock);
  ret = q->length[q->read];
  spin_unlock(&dqueue_lock);
  return (ret);
}

void
diva_data_q_ack_segment4read(diva_um_idi_data_queue_t* q)
{
  spin_lock(&dqueue_lock);
  if (q->count) {
    q->length[q->read] = 0;
    q->count--;
    q->read++;
    if (q->read >= q->segments) {
      q->read = 0;
    }
  }
  spin_unlock(&dqueue_lock);
}

