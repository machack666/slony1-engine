/*-------------------------------------------------------------------------
 * dbutils.c
 *
 *	Database utility functions for Slony-I
 *
 *	Copyright (c) 2003-2004, PostgreSQL Global Development Group
 *	Author: Jan Wieck, Afilias USA INC.
 *
 *	$Id: dbutils.c,v 1.7 2004-02-24 21:03:34 wieck Exp $
 *-------------------------------------------------------------------------
 */


#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>

#include "libpq-fe.h"
#include "c.h"

#include "slon.h"


static int	slon_appendquery_int(SlonDString *dsp, char *fmt, va_list ap);


/* ----------
 * slon_connectdb
 * ----------
 */
SlonConn *
slon_connectdb(char *conninfo, char *symname)
{
	PGconn	   *dbconn;
	SlonConn   *conn;

	/*
	 * Create the native database connection
	 */
	dbconn = PQconnectdb(conninfo);
	if (dbconn == NULL)
	{
		slon_log(SLON_ERROR,
				"slon_connectdb: PQconnectdb(\"%s\") failed\n",
				conninfo);
		return NULL;
	}
	if (PQstatus(dbconn) != CONNECTION_OK)
	{
		slon_log(SLON_ERROR,
				"slon_connectdb: PQconnectdb(\"%s\") failed - %s",
				conninfo, PQerrorMessage(dbconn));
		PQfinish(dbconn);
		return NULL;
	}

	/*
	 * Embed it into a SlonConn structure used to exchange it with
	 * the scheduler. On return this new connection object is locked.
	 */
	conn = slon_make_dummyconn(symname);
	conn->dbconn = dbconn;

	return conn;
}


/* ----------
 * slon_disconnectdb
 * ----------
 */
void
slon_disconnectdb(SlonConn *conn)
{
	/*
	 * Disconnect the native database connection
	 */
	PQfinish(conn->dbconn);

	/*
	 * Unlock and destroy the condition and mutex variables
	 * and free memory.
	 */
	slon_free_dummyconn(conn);
}


/* ----------
 * slon_make_dummyconn
 * ----------
 */
SlonConn *
slon_make_dummyconn(char *symname)
{
	SlonConn   *conn;

	/*
	 * Allocate and initialize the SlonConn structure
	 */
	conn = (SlonConn *)malloc(sizeof(SlonConn));
	if (conn == NULL)
	{
		perror("slon_make_dummyconn: malloc()");
		slon_abort();
	}
	memset(conn, 0, sizeof(SlonConn));
	conn->symname = strdup(symname);

	/* 
	 * Initialize and lock the condition and mutex variables
	 */
	pthread_mutex_init(&(conn->conn_lock), NULL);
	pthread_cond_init(&(conn->conn_cond), NULL);
	pthread_mutex_lock(&(conn->conn_lock));
	
	return conn;
}


/* ----------
 * slon_free_dummyconn
 * ----------
 */
void
slon_free_dummyconn(SlonConn *conn)
{
	/*
	 * Destroy and unlock the condition and mutex variables
	 */
	pthread_cond_destroy(&(conn->conn_cond));
	pthread_mutex_unlock(&(conn->conn_lock));
	pthread_mutex_destroy(&(conn->conn_lock));

	/*
	 * Free allocated memory
	 */
	free(conn->symname);
	free(conn);
}


/* ----------
 * db_getLocalNodeId
 *
 *	Query a connection for the value of sequence sl_local_node_id
 * ----------
 */
int
db_getLocalNodeId(PGconn *conn)
{
	char		query[1024];
	PGresult   *res;
	int			retval;

	/*
	 * Select the last_value from the sl_local_node_id sequence
	 */
	snprintf(query, 1024, "select last_value::int4 from %s.sl_local_node_id",
			rtcfg_namespace);
	res = PQexec(conn, query);
	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		slon_log(SLON_ERROR,
				"cannot get sl_local_node_id - %s",
				PQresultErrorMessage(res));
		PQclear(res);
		return -1;
	}
	if (PQntuples(res) != 1)
	{
		slon_log(SLON_ERROR,
				"query '%s' returned %d rows (expected 1)\n",
				query, PQntuples(res));
		PQclear(res);
		return -1;
	}

	/*
	 * Return the result as an integer value
	 */
	retval = strtol(PQgetvalue(res, 0, 0), NULL, 10);
	PQclear(res);
	
	return retval;
}


/* ----------
 * slon_mkquery
 *
 *	A simple query formatting and quoting function using dynamic string
 *	buffer allocation.
 *	Similar to sprintf() it uses formatting symbols:
 *		%s		String argument
 *		%q		Quoted literal (\ and ' will be escaped)
 *		%d		Integer argument
 * ----------
 */
int
slon_mkquery(SlonDString *dsp, char *fmt, ...)
{
	va_list		ap;

	dstring_reset(dsp);

	va_start(ap, fmt);
	slon_appendquery_int(dsp, fmt, ap);
	va_end(ap);

	dstring_terminate(dsp);

	return 0;
}


/* ----------
 * slon_appendquery
 *
 *	Append query string material to an existing dynamic string.
 * ----------
 */
int
slon_appendquery(SlonDString *dsp, char *fmt, ...)
{
	va_list		ap;

	va_start(ap, fmt);
	slon_appendquery_int(dsp, fmt, ap);
	va_end(ap);

	dstring_terminate(dsp);

	return 0;
}


/* ----------
 * slon_appendquery_int
 *
 *	Implementation of slon_mkquery() and slon_appendquery().
 * ----------
 */
static int
slon_appendquery_int(SlonDString *dsp, char *fmt, va_list ap)
{
	char	   *s;
	char		buf[64];

	while (*fmt)
	{
		switch(*fmt)
		{
			case '%':
				fmt++;
				switch(*fmt)
				{
					case 's':	s = va_arg(ap, char *);
								dstring_append(dsp, s);
								fmt++;
								break;

					case 'q':	s = va_arg(ap, char *);
								while (*s != '\0')
								{
									switch (*s)
									{
										case '\'':
											dstring_addchar(dsp, '\'');
											break;
										case '\\':
											dstring_addchar(dsp, '\\');
											break;
										default:
											break;
									}
									dstring_addchar(dsp, *s);
									s++;
								}
								fmt++;
								break;

					case 'd':	sprintf(buf, "%d", va_arg(ap, int));
								dstring_append(dsp, buf);
								fmt++;
								break;

					default:	dstring_addchar(dsp, '%');
								dstring_addchar(dsp, *fmt);
								fmt++;
								break;
				}
				break;

			case '\\':	fmt++;
						dstring_addchar(dsp, *fmt);
						fmt++;
						break;

			default:	dstring_addchar(dsp, *fmt);
						fmt++;
						break;
		}
	}

	dstring_terminate(dsp);

	return 0;
}


