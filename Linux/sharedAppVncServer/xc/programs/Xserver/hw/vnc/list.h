/* List.h
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

#ifndef __LIST_H__
#define __LIST_H__

typedef struct _Node
{
  void* data;
  struct _Node* next;
  struct _Node* prev;
} Node;

typedef struct _List
{
  Node *head;
  Node *tail;
  int count;
} List;

Node* Node_New(void *data);
List* List_New();
void* List_Element(List *l, int index);
int List_HasData(List *l, void* data);
int List_Add( List *l, void* data );
int List_Insert( List *l, int index, void* data);
void* List_Remove_Index(List *l, int index);
int List_Remove_Data(List *l, void* data);
void List_Remove_Node(List *l, Node*);
int List_Count(List *l); 
void List_Delete(List *l);

#endif
