/*-------------------------------------------------------------------------
 * local_listen.c
 *
 *	Implementation of the thread listening for events on
 *	the local node database.
 *
 *	Copyright (c) 2003-2004, PostgreSQL Global Development Group
 *	Author: Jan Wieck, Afilias USA INC.
 *
 *	$Id: local_listen.c,v 1.6 2004-02-25 19:47:37 wieck Exp $
 *-------------------------------------------------------------------------
 */


#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>

#include "libpq-fe.h"
#include "c.h"

#include "slon.h"



/* ----------
 * slon_localListenThread
 *
 *	Listen for events on the local database connection. This means,
 *	events generated by the local node only.
 * ----------
 */
void *
localListenThread_main(void *dummy)
{
	SlonConn   *conn;
	SlonDString	query1;
	PGconn	   *dbconn;
	PGresult   *res;
	int			ntuples;
	int			tupno;
	PGnotify   *notification;

	/*
	 * Connect to the local database
	 */
	if ((conn = slon_connectdb(rtcfg_conninfo, "local_listen")) == NULL)
		slon_abort();
	dbconn = conn->dbconn;

	/*
	 * Initialize local data
	 */
	dstring_init(&query1);

	/*
	 * Listen for local events
	 */
	slon_mkquery(&query1, "listen \"_%s_Event\"", rtcfg_cluster_name);
	res = PQexec(dbconn, dstring_data(&query1));
	if (PQresultStatus(res) != PGRES_COMMAND_OK)
	{
		slon_log(SLON_FATAL,
				"localListenThread: \"%s\" - %s",
				dstring_data(&query1), PQresultErrorMessage(res));
		PQclear(res);
		dstring_free(&query1);
		slon_abort();
	}
	PQclear(res);
			
	slon_log(SLON_DEBUG1,
			"localListenThread: listening for \"%s_Event\"\n",
			rtcfg_cluster_name);

	/*
	 * Process all events, then wait for notification and repeat
	 * until shutdown time has arrived.
	 */
	while (true)
	{
		/*
		 * Start the transaction
		 */
		res = PQexec(dbconn, "start transaction; "
					"set transaction isolation level serializable;");
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			slon_log(SLON_FATAL,
					"localListenThread: cannot start transaction - %s",
					PQresultErrorMessage(res));
			PQclear(res);
			dstring_free(&query1);
			slon_abort();
			break;
		}
		PQclear(res);

		/*
		 * Drain notifications.
		 */
		PQconsumeInput(dbconn);
		while ((notification = PQnotifies(dbconn)) != NULL)
			PQfreemem(notification);

		/*
		 * Query the database for new local events
		 */
		slon_mkquery(&query1, 
				"select ev_seqno, ev_timestamp, "
				"       ev_minxid, ev_maxxid, ev_xip, "
				"       ev_type, "
				"       ev_data1, ev_data2, ev_data3, ev_data4, "
				"       ev_data5, ev_data6, ev_data7, ev_data8 "
				"from %s.sl_event "
				"where  ev_origin = '%d' "
				"       and ev_seqno > '%s' "
				"order by ev_seqno",
				rtcfg_namespace, rtcfg_nodeid, rtcfg_lastevent);
		res = PQexec(dbconn, dstring_data(&query1));
		if (PQresultStatus(res) != PGRES_TUPLES_OK)
		{
			slon_log(SLON_FATAL,
					"localListenThread: \"%s\" - %s",
					dstring_data(&query1), PQresultErrorMessage(res));
			PQclear(res);
			dstring_free(&query1);
			slon_abort();
			break;
		}
		ntuples = PQntuples(res);

		for (tupno = 0; tupno < ntuples; tupno++)
		{
			char   *ev_type;

			/*
			 * Remember the event sequence number for confirmation
			 */
			strcpy(rtcfg_lastevent, PQgetvalue(res, tupno, 0));

			/*
			 * Get the event type and process configuration events
			 */
			ev_type = PQgetvalue(res, tupno, 5);
			if (strcmp(ev_type, "SYNC") == 0)
			{
				/*
				 * SYNC - nothing to do
				 */
			}
			else if (strcmp(ev_type, "STORE_NODE") == 0)
			{
				/*
				 * STORE_NODE
				 */
				int		no_id;
				char   *no_comment;

				no_id		= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);
				no_comment	= PQgetvalue(res, tupno, 7);

				if (no_id != rtcfg_nodeid)
					rtcfg_storeNode(no_id, no_comment);
			}
			else if (strcmp(ev_type, "ENABLE_NODE") == 0)
			{
				/*
				 * ENABLE_NODE
				 */
				int		no_id;

				no_id		= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);

				if (no_id != rtcfg_nodeid)
					rtcfg_enableNode(no_id);
			}
			else if (strcmp(ev_type, "STORE_PATH") == 0)
			{
				/*
				 * STORE_PATH
				 */
				int		pa_server;
				int		pa_client;
				char   *pa_conninfo;
				int		pa_connretry;

				pa_server	= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);
				pa_client	= (int) strtol(PQgetvalue(res, tupno, 7), NULL, 10);
				pa_conninfo	= PQgetvalue(res, tupno, 8);
				pa_connretry = (int) strtol(PQgetvalue(res, tupno, 9), NULL, 10);

				if (pa_client == rtcfg_nodeid)
					rtcfg_storePath(pa_server, pa_conninfo, pa_connretry);
			}
			else if (strcmp(ev_type, "STORE_LISTEN") == 0)
			{
				/*
				 * STORE_LISTEN
				 */
				int		li_origin;
				int		li_provider;
				int		li_receiver;

				li_origin	= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);
				li_provider	= (int) strtol(PQgetvalue(res, tupno, 7), NULL, 10);
				li_receiver	= (int) strtol(PQgetvalue(res, tupno, 8), NULL, 10);

				if (li_receiver == rtcfg_nodeid)
					rtcfg_storeListen(li_origin, li_provider);
			}
			else if (strcmp(ev_type, "STORE_SET") == 0)
			{
				/*
				 * STORE_SET
				 */
				int		set_id;
				int		set_origin;
				char   *set_comment;

				set_id		= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);
				set_origin	= (int) strtol(PQgetvalue(res, tupno, 7), NULL, 10);
				set_comment	= PQgetvalue(res, tupno, 8);

				rtcfg_storeSet(set_id, set_origin, set_comment);
			}
			else if (strcmp(ev_type, "SUBSCRIBE_SET") == 0)
			{
				/*
				 * SUBSCRIBE_SET
				 */
				int		sub_set;
				int		sub_provider;
				int		sub_receiver;
				char   *sub_forward;

				sub_set			= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);
				sub_provider	= (int) strtol(PQgetvalue(res, tupno, 7), NULL, 10);
				sub_receiver	= (int) strtol(PQgetvalue(res, tupno, 8), NULL, 10);
				sub_forward		= PQgetvalue(res, tupno, 9);

				if (sub_receiver == rtcfg_nodeid)
					rtcfg_storeSubscribe(sub_set, sub_provider, sub_forward);
			}
			else if (strcmp(ev_type, "ENABLE_SUBSCRIPTION") == 0)
			{
				/*
				 * ENABLE_SUBSCRIPTION
				 */
				int		sub_set;
				int		sub_provider;
				int		sub_receiver;
				char   *sub_forward;

				sub_set			= (int) strtol(PQgetvalue(res, tupno, 6), NULL, 10);
				sub_provider	= (int) strtol(PQgetvalue(res, tupno, 7), NULL, 10);
				sub_receiver	= (int) strtol(PQgetvalue(res, tupno, 8), NULL, 10);
				sub_forward		= PQgetvalue(res, tupno, 9);

				if (sub_receiver == rtcfg_nodeid)
					rtcfg_enableSubscription(sub_set);
			}
			else
			{
				slon_log(SLON_FATAL,
						"localListenThread: event %s: Unknown event type %s\n", 
						rtcfg_lastevent, ev_type);
				slon_abort();
			}
		}

		PQclear(res);

		/*
		 * If there where events, commit the transaction.
		 */
		if (ntuples > 0)
		{
			res = PQexec(dbconn, "commit transaction");
			if (PQresultStatus(res) != PGRES_COMMAND_OK)
			{
				slon_log(SLON_FATAL,
						"localListenThread: \"%s\" - %s",
						dstring_data(&query1), PQresultErrorMessage(res));
				PQclear(res);
				dstring_free(&query1);
				slon_abort();
				break;
			}
			PQclear(res);
		}
		else
		{
			/*
			 * No database events received. Rollback instead.
			 */
			res = PQexec(dbconn, "rollback transaction;");
			if (PQresultStatus(res) != PGRES_COMMAND_OK)
			{
				slon_log(SLON_FATAL,
						"localListenThread: \"rollback transaction;\" - %s",
						PQresultErrorMessage(res));
				PQclear(res);
				slon_abort();
				break;
			}
			PQclear(res);
		}

		/*
		 * Wait for notify
		 */
		if (sched_wait_conn(conn, SCHED_WAIT_SOCK_READ) != SCHED_STATUS_OK)
			break;
	}

	/*
	 * The scheduler asked us to shutdown. Free memory
	 * and close the DB connection.
	 */
	dstring_free(&query1);
	slon_disconnectdb(conn);
	slon_log(SLON_DEBUG1, "localListenThread: thread done\n");
	pthread_exit(NULL);
}


