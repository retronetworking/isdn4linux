/* $Id$ */

#include "platform.h"
#include "dlist.h"

static spinlock_t dlist_lock;

/*
**  Initialize linked list
*/

void
diva_q_init (diva_entity_queue_t* q)
{
  memset (q, 0x00, sizeof(*q));
  dlist_lock = SPIN_LOCK_UNLOCKED;
}

/*
**  Remove element from linked list
*/
void
diva_q_remove (diva_entity_queue_t* q, diva_entity_link_t* what)
{
  spin_lock(&dlist_lock);

  if(!what->prev) {
    if ((q->head = what->next)) {
      q->head->prev = 0;
    } else {
      q->tail = 0;
    }
  } else if (!what->next) {
    q->tail = what->prev;
    q->tail->next = 0;
  } else {
    what->prev->next = what->next;
    what->next->prev = what->prev;
  }
  what->prev = what->next = 0;

  spin_unlock(&dlist_lock);
}

/*
**  Add element to the tail of linked list
*/
void
diva_q_add_tail (diva_entity_queue_t* q, diva_entity_link_t* what)
{
  spin_lock(&dlist_lock);

  what->next = 0;
  if (!q->head) {
    what->prev = 0;
    q->head = q->tail = what;
  } else {
    what->prev = q->tail;
    q->tail->next = what;
    q->tail = what;
  }

  spin_unlock(&dlist_lock);
}

diva_entity_link_t*
diva_q_find (const diva_entity_queue_t* q, const void* what,
             diva_q_cmp_fn_t cmp_fn)
{
  diva_entity_link_t* current;

  spin_lock(&dlist_lock);
  current = q->head;
  while (current) {
    if (!(*cmp_fn)(what, current)) {
      break;
    }
    current = current->next;
  }
  spin_unlock(&dlist_lock);

  return (current);
}

diva_entity_link_t*
diva_q_get_head (diva_entity_queue_t* q)
{
  return (q->head);
}

diva_entity_link_t*
diva_q_get_tail (diva_entity_queue_t* q)
{
  return (q->tail);
}

diva_entity_link_t*
diva_q_get_next	(diva_entity_link_t* what)
{
  return ((what) ? what->next : 0);
}

diva_entity_link_t*
diva_q_get_prev	(diva_entity_link_t* what)
{
  return ((what) ? what->prev : 0);
}

int
diva_q_get_nr_of_entries (const diva_entity_queue_t* q)
{
  int i = 0;
  const diva_entity_link_t* current;

  spin_lock(&dlist_lock);
  current = q->head;
  while (current) {
    i++;
    current = current->next;
  }
  spin_unlock(&dlist_lock);

  return (i);
}

