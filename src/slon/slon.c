/*-------------------------------------------------------------------------
 * slon.c
 *
 *	The control framework for the node daemon.
 *
 *	Copyright (c) 2003, PostgreSQL Global Development Group
 *	Author: Jan Wieck, Afilias USA INC.
 *
 *	$Id: slon.c,v 1.2 2003-12-03 19:29:53 wieck Exp $
 *-------------------------------------------------------------------------
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "slon.h"

int
main (int argc, const char *argv[])
{
	printf("hello world\n");
	return 0;
}
