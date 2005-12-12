/* List.c
 *
 * Copyright (C) 2004 Grant Wallace, gwallace@cs.princeton.edu
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include "list.h"

Node *Node_New(void* data)
{
  Node *node = (Node*) malloc(sizeof(Node));
  node->next = NULL;
  node->prev = NULL;
  node->data = data;
  return node;
}


List *List_New()
{
  List *l = (List*) malloc(sizeof(List));
  l->head = NULL;
  l->tail = NULL;
  l->count = 0;
  return l;
}

int List_Add(List *l, void* data )
{
  Node* nd = Node_New(data);

  if ( l->tail == NULL )
  {
    l->head = nd;
    l->tail = nd;
  } else {
    l->tail->next = nd;
    nd->prev = l->tail; 
    l->tail = nd;
  }
  
  l->count++;
  return 1;
}


int List_Insert( List *l, int index, void* data )
{
  Node *nd = l->head;
  int result;
  int i;

  for ( i=0 ; i < index ; i++ )
  {
    if (nd != NULL)
      nd = nd->next;
    else
      break;
  }

  if (nd==NULL)
  {
    /* index out of range - append to end */
    result = List_Add(l, data);
    return result;
  }
  else
  {
    /* insert data at this index */
    Node* newNd = Node_New(data);

    newNd->next = nd;
    newNd->prev = nd->prev;
    nd->prev = newNd;
    if (index != 0)
    {
      newNd->prev->next = newNd;
    } else {
      l->head = newNd;
    }

  }
  
  l->count++;
  return 1;
}


void* List_Element( List *l, int index )
{
  Node *nd;
  int i;

  if ( l->head != NULL )
  {
    nd = l->head;
    for ( i=0 ; i < index ; i++ )
    {
      if (nd == NULL) {return NULL;}
      nd = nd->next;
    }

    if (nd) { return nd->data; }
  }
  return NULL;
}

int List_HasData(List *l, void* data)
{
  Node *nd;

  nd = l->head;
  while( nd != NULL )
  {
    if ( nd->data == data ) 
    {
      return 1;
    }
    nd = nd->next;
  }

  return 0;
}

int List_Remove_Data( List *l, void* data )
{
  Node *nd;
  int i=0;

  nd = l->head;
  while( nd != NULL )
  {
    if ( nd->data == data ) 
    {
      Node *prev_nd = nd->prev;
      List_Remove_Node(l, nd);
      free(nd);
      nd = prev_nd;
      l->count--;
      i++;;
    }
    if (nd) nd = nd->next;
  }

  return i;
}

void* List_Remove_Index( List *l, int index )
{
  Node *nd;
  void *data;
  int i;

  if ( l->head != NULL )
  {
    nd = l->head;
    for ( i=0 ; i < index ; i++ )
    {
      if (nd == NULL) {return NULL;}
      nd = nd->next;
    }

    if (nd)
    {
      data = nd->data;
      List_Remove_Node(l, nd);
      free(nd);
      l->count--;
      return data;
    }
  }

  return NULL;
}

void List_Remove_Node( List *l, Node* nd )
{
  if ( nd )
  {
    if ( nd->prev == NULL )
    {  
      /* this is the head */
      l->head = nd->next;
    } else {
      nd->prev->next = nd->next;
    }

    if ( nd->next == NULL )
    {
      /* this is the tail */
      l->tail = nd->prev;
    } else {
      nd->next->prev = nd->prev;
    }
  }
}

int List_Count(List *l)
{
  return l->count;
}

void List_Delete(List *l)
{
  void *data;

  while( l->head )
  {
    data = List_Remove_Index(l, 0);
    /*free(data); */
  }

  free(l);
}
  


