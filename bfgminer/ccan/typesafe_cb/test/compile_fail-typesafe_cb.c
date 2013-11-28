/*
 * Copyright 2011 Rusty Russell
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.  See LICENSE for more details.
 */

#include <ccan/typesafe_cb/typesafe_cb.h>
#include <stdlib.h>

static void _register_callback(void (*cb)(void *arg), void *arg)
{
}

#define register_callback(cb, arg)				\
	_register_callback(typesafe_cb(void, void *, (cb), (arg)), (arg))

static void my_callback(char *p)
{
}

int main(int argc, char *argv[])
{
	char str[] = "hello world";
#ifdef FAIL
	int *p;
#if !HAVE_TYPEOF||!HAVE_BUILTIN_CHOOSE_EXPR||!HAVE_BUILTIN_TYPES_COMPATIBLE_P
#error "Unfortunately we don't fail if typesafe_cb_cast is a noop."
#endif
#else
	char *p;
#endif
	p = NULL;

	/* This should work always. */
	register_callback(my_callback, str);

	/* This will fail with FAIL defined */
	register_callback(my_callback, p);
	return 0;
}
