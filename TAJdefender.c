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
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#define DEFAULT_HEAP_SIZE 8388608 //8MB in bytes
#define DLOPEN_FAILED  120
#define SECCOMP_FAILED 121
#define EXIT_FAILED    122
#define MMAP_FAILED    123

struct cxa_atexit_handler
{
	union
	{
	    void (*atexit_fn)(void);
		void (*cxa_atexit_fn)(void *);
	}f;
	void *arg;
	int type;
};
#define MAX_ATEXIT_HANDLERS 1024
static struct cxa_atexit_handler s_atexit_handlers[MAX_ATEXIT_HANDLERS];
static int s_atexit_handler_count;

static void (*real_init)(void);
static int (*real_main)(int, char **, char **);
static void (*real_fini)(void);
static void (*real_rtld_fini)(void);

static void reimp_fini(void);
static void reimp_rtld_fini(void);

static int s_ran_fini;
static int s_ran_rtld_fini;

static char *s_heap;
static size_t s_heapsize;
static char *s_brk;

void *sbrk(intptr_t incr)
{
	intptr_t used, remaining;
	void *newbrk;

	if(s_brk==0)
		s_brk=s_heap;

	used=s_brk-s_heap;
	remaining=s_heapsize-used;

	if(remaining<incr)
	{
		errno=ENOMEM;
		return (void*)-1;
	}

	newbrk=s_brk;
	s_brk+=incr;
	return newbrk;
}

void exit(int exit_code)
{
	while(s_atexit_handler_count>0)
	{
		struct cxa_atexit_handler *handler;
		s_atexit_handler_count--;
		handler = &s_atexit_handlers[s_atexit_handler_count];
		switch(handler->type)
		{
            case 0:
                handler->f.atexit_fn();
                break;
            case 1:
                handler->f.cxa_atexit_fn(handler->arg);
                break;
        }
	}

	reimp_fini();
	reimp_rtld_fini();

	fflush(stdout);
	fflush(stderr);

	while(1)
		syscall(SYS_exit, exit_code);
}

int __cxa_atexit(void (*func)(void *),void *arg,void *dso_handle)
{
	struct cxa_atexit_handler *handler;
	if (s_atexit_handler_count>=MAX_ATEXIT_HANDLERS)
		return -1;

	handler=&s_atexit_handlers[s_atexit_handler_count];
	handler->f.cxa_atexit_fn=func;
	handler->arg=arg;
	handler->type=1;
	s_atexit_handler_count++;
	return 0;
}

int atexit(void (*func)(void))
{
	struct cxa_atexit_handler *handler;
	if (s_atexit_handler_count>=MAX_ATEXIT_HANDLERS)
		return -1;

	handler=&s_atexit_handlers[s_atexit_handler_count];
	handler->f.atexit_fn=func;
	handler->arg=0;
	handler->type=0;
	s_atexit_handler_count++;
	return 0;
}

static void reimp_init(void)
{
	int stdin_flags;
	int c;

	fprintf(stdout, "<<entering SECCOMP mode>>\n");
	fflush(stdout);
	fprintf(stderr, "<<entering SECCOMP mode>>\n");
	fflush(stderr);

	stdin_flags=fcntl(0, F_GETFL, 0);
	fcntl(0, F_SETFL, stdin_flags | O_NONBLOCK);
	c=fgetc(stdin);
	if (c != EOF)
		ungetc(c, stdin);

	fcntl(0, F_SETFL, stdin_flags);

	if (prctl(PR_SET_SECCOMP, 1, 0, 0) == -1)
		_exit(SECCOMP_FAILED);

	real_init();
}

static int reimp_main(int argc, char **argv, char **envp)
{
	int n;
	n=real_main(argc, argv, envp);
	exit(n);
	return EXIT_FAILED;
}

static void reimp_fini(void)
{
	if(!s_ran_fini)
	{
		fflush(stdout);
		s_ran_fini=1;
		real_fini();
	}
}

static void reimp_rtld_fini(void)
{
	if (!s_ran_rtld_fini)
	{
		fflush(stdout);
		s_ran_rtld_fini=1;
		real_rtld_fini();
	}
}

int __libc_start_main(
	int (*main)(int, char **, char **),
	int argc,
	char ** ubp_av,
	void (*init)(void),
	void (*fini)(void),
	void (*rtld_fini)(void),
	void (* stack_end))
{
	void *libc_handle;
	const char *heapenv;

	int (*real_libc_start_main)(
		int (*main) (int, char **, char **),
		int argc,
		char ** ubp_av,
		void (*init)(void),
		void (*fini)(void),
		void (*rtld_fini)(void),
		void (* stack_end));

	real_init=init;
	real_main=main;
	real_fini=fini;
	real_rtld_fini=rtld_fini;

	heapenv=getenv("EASYSANDBOX_HEAPSIZE");
	s_heapsize=(size_t)((heapenv!=0)?atol(heapenv):DEFAULT_HEAP_SIZE);
	s_heap=mmap(0, s_heapsize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

	if(s_heap == MAP_FAILED)
		_exit(MMAP_FAILED);

	libc_handle=dlopen("libc.so.6", RTLD_LOCAL | RTLD_LAZY);
	if(libc_handle == 0)
		_exit(DLOPEN_FAILED);

	*(void **) (&real_libc_start_main)=dlsym(libc_handle, "__libc_start_main");

	return real_libc_start_main(reimp_main, argc, ubp_av,reimp_init, reimp_fini, reimp_rtld_fini, stack_end);
}
