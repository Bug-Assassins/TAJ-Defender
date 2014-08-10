/*
 * Copyright (C) 2014 BugAssassins
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file is a part of TAJ Online Judge (TAJ-Defender), developed
 * @National Institute of Technology Karnataka, Surathkal
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#define MIN_ALLOC 65536
#define ALLOCATED 1

union Header
{
	struct
	{
		union Header *prev,*next;
		size_t size;
		int flags;
    }h;
	long align;
};

static union Header *s_head,*s_tail;
static inline size_t round_to_multiple(size_t n,size_t multiple)
{
	if(n%multiple!=0)
	{
		n+=multiple-(n%multiple);
	}
	return n;
}

static inline int is_allocated(union Header *block)
{
    if((block->h.flags&ALLOCATED)==0)
        return 0;
    else
        return 1;
}

static union Header *block_alloc(size_t b_size)
{
	union Header *block;

	if(b_size<MIN_ALLOC)
		b_size=MIN_ALLOC;


	block=sbrk((intptr_t)b_size);
	if(block==(void *)-1)
		return 0;

	block->h.next=0;

	if(s_head==0)
	{
		s_head=s_tail=block;
		block->h.prev=0;
	}
	else
	{
		s_tail->h.next=block;
		block->h.prev=s_tail;
		s_tail=block;
	}

	block->h.size=b_size;
	block->h.flags=0;

	return block;
}

static void split_block_if_necessary(union Header *block, size_t required_block_size)
{
	union Header *excess;
	size_t left_over;

	left_over=block->h.size-required_block_size;

	if(left_over<=sizeof(union Header))
		return;

	block->h.size=required_block_size;
	excess=(union Header *)(((char *) block)+required_block_size);

	excess->h.size=left_over;
	excess->h.flags=0;

	excess->h.next=block->h.next;
	excess->h.prev=block;

	if(block->h.next!=0)
		block->h.next->h.prev=excess;
	else
		s_tail=excess;

	block->h.next=excess;
}

static void join_if_necessary(union Header *block)
{
	union Header *succ;

	if(block==0)
		return;
	succ=block->h.next;

	if(is_allocated(block)||succ==0||is_allocated(succ))
		return;

	block->h.size+=succ->h.size;

	if(succ->h.next!=0)
		succ->h.next->h.prev = block;
	else
		s_tail = block;

	block->h.next=block->h.next->h.next;
}

void *malloc(size_t size)
{
	size_t required_block_size;
	union Header *block;

	required_block_size=round_to_multiple(size+sizeof(union Header),sizeof(union Header));

	for(block=s_head;block!=0;block=block->h.next)
	{
		if (block->h.size>=required_block_size&&!is_allocated(block))
			break;
	}

	if(block==0)
	{
		block=block_alloc(required_block_size);
		if(block==0)
		{
			errno=ENOMEM;
			return 0;
		}
	}

	split_block_if_necessary(block,required_block_size);

	block->h.flags|=ALLOCATED;
	return (void*)(block + 1);
}
void free(void *p)
{
	union Header *block;

	if(p==0)
		return;

	block=((union Header *)p)-1;

	if(!is_allocated(block))
	{
		fprintf(stderr,"Invalid free at %p\n", p);
		return;
	}

	block->h.flags&=~(ALLOCATED);
	join_if_necessary(block);
	join_if_necessary(block->h.prev);
}
void *calloc(size_t nmemb, size_t size)
{
	void *buf;

	buf=malloc(nmemb * size);
	if(buf!=0)
		memset(buf, 0, nmemb * size);

	return buf;
}
void *realloc(void *ptr, size_t size)
{
	union Header *block;
	size_t to_copy;
	void *buf;

	if(ptr==0)
		return malloc(size);

	if(size==0)
	{
		free(ptr);
		return 0;
	}

	block=((union Header *)ptr) - 1;

	buf=malloc(size);
	if(buf == 0)
		return 0;

	to_copy=block->h.size-sizeof(union Header);
	if(to_copy>size)
		to_copy=size;

	memcpy(buf, ptr, to_copy);

	free(ptr);

	return buf;
}
