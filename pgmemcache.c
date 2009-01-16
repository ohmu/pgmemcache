/*
 * PostgreSQL functions to interface with memcache.
 *
 * Copyright (c) 2004-2005 Sean Chittenden <sean@chittenden.org>
 * Copyright (c) 2007-2008 Neil Conway <neilc@samurai.com>
 * Copyright (c) 2007 Open Technology Group, Inc. <http://www.otg-nc.com>
 * Copyright (c) 2008 Hannu Valtonen <hannu.valtonen@hut.fi>
 *
 * $PostgreSQL$
 * 
 * See the file COPYING for distribution terms.
 */

#include "postgres.h"

#include <inttypes.h>

#include "access/heapam.h"
#include "access/htup.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/datetime.h"
#include "utils/guc.h"
#include "utils/memutils.h"

#include "pgmemcache.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/*
 * PostgreSQL <= 8.2 provided VARATT_SIZEP(p), but later versions use
 * SET_VARSIZE() (and also define the VARATT_SIZEP_DEPRECATED
 * symbol). Therefore, define the latter in terms of the former if
 * necessary.
 */
#ifndef SET_VARSIZE
#define SET_VARSIZE(ptr, size) VARATT_SIZEP(ptr) = (size)
#endif

/* Per-backend global state. */
struct memcache_global
{
    memcached_st *mc;

    /* context in which long-lived state is allocated */
    MemoryContext pg_ctxt;
};

static struct memcache_global globals;

/* Custom GUC variable */
static char *memcache_default_servers;

static const char *assign_default_servers_guc(const char *newval,
                                              bool doit, GucSource source);
static const char *show_default_servers_guc(void);
static Datum       memcache_atomic_op(bool increment, PG_FUNCTION_ARGS);
static Datum       memcache_set_cmd(int type, PG_FUNCTION_ARGS);
static memcached_return do_server_add(char *host_str);

/* NOTICE!! form_server_tuple if 0'd out because the definition's been if 0'd at the end of the file */
#if 0 
static HeapTuple   form_server_tuple(memcached_server_st *serv,
                                     TupleDesc tdesc);
#endif

static bool        do_memcache_set_cmd(int type, char *key, size_t key_len,
                                       char *val, size_t val_len, time_t expire);
static time_t      interval_to_time_t(Interval *span);

#define PG_MEMCACHE_ADD					0x0001
#define PG_MEMCACHE_REPLACE				0x0002
#define PG_MEMCACHE_SET					0x0004

#define PG_MEMCACHE_TYPE_INTERVAL		0x0010
#define PG_MEMCACHE_TYPE_TIMESTAMP      0x0020

void
_PG_init(void)
{
    MemoryContext old_ctxt;

    globals.pg_ctxt = AllocSetContextCreate(TopMemoryContext,
                                            "pgmemcache global context",
                                            ALLOCSET_SMALL_MINSIZE,
                                            ALLOCSET_SMALL_INITSIZE,
                                            ALLOCSET_SMALL_MAXSIZE);

    old_ctxt = MemoryContextSwitchTo(globals.pg_ctxt);
    globals.mc = memcached_create(NULL);
	memcached_behavior_set(globals.mc, MEMCACHED_BEHAVIOR_VERIFY_KEY, 1);
	
    MemoryContextSwitchTo(old_ctxt);

    DefineCustomStringVariable("pgmemcache.default_servers",
                               "Comma-separated list of memcached servers to connect to.",
                               "Specified as a comma-separated list of host:port (port is optional).",
                               &memcache_default_servers,
                               NULL,
                               PGC_USERSET, 
                               GUC_LIST_INPUT,
                               assign_default_servers_guc,
                               show_default_servers_guc);
}

static const char *
assign_default_servers_guc(const char *newval, bool doit, GucSource source)
{
	do_server_add((char *) newval);
    return newval;
}

static const char *
show_default_servers_guc(void)
{
    return memcache_default_servers;
}

/*
 * This is called when we're being unloaded from a process. Note that
 * this only happens when we're being replaced by a LOAD (e.g. it
 * doesn't happen on process exit), so we can't depend on it being
 * called.
 */
void
_PG_fini(void)
{
    memcached_free(globals.mc);
    MemoryContextDelete(globals.pg_ctxt);
}

Datum
memcache_add(PG_FUNCTION_ARGS)
{
    return memcache_set_cmd(PG_MEMCACHE_ADD | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}


Datum
memcache_add_absexpire(PG_FUNCTION_ARGS)
{
    return memcache_set_cmd(PG_MEMCACHE_ADD | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

static Datum
memcache_atomic_op(bool increment, PG_FUNCTION_ARGS)
{
    text *atomic_key = PG_GETARG_TEXT_P(0);
	char *key;
    size_t key_length;
    uint64_t val;
	unsigned int offset = 1;
	memcached_return rc;
	
	key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(atomic_key)));
    key_length = strlen(key);
	
	if (key_length < 1)
		elog(ERROR, "memcache key cannot be an empty string");
	if (key_length >= 250)
		elog(ERROR, "memcache key too long");

    if (PG_NARGS() >= 2)
        offset = PG_GETARG_UINT32(1);

    if (increment)
		rc = memcached_increment (globals.mc, key, key_length, offset, &val);
	else
		rc = memcached_decrement (globals.mc, key, key_length, offset, &val);

	if (rc != MEMCACHED_SUCCESS)
		elog(ERROR, "%s ", memcached_strerror(globals.mc, rc));
	
    PG_RETURN_UINT32(val);
}

Datum
memcache_decr(PG_FUNCTION_ARGS)
{
    return memcache_atomic_op(false, fcinfo);
}

Datum
memcache_delete(PG_FUNCTION_ARGS)
{
    text *key_to_be_deleted = PG_GETARG_TEXT_P(0);
    size_t key_length;
    time_t hold;
	memcached_return rc;
	char *key;

	key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(key_to_be_deleted)));
    key_length = strlen(key);
	if (key_length < 1)
		elog(ERROR, "memcache key cannot be an empty string");
	if (key_length >= 250)
		elog(ERROR, "memcache key too long");

	hold = (time_t) 0.0;
    if (PG_NARGS() >= 2 && PG_ARGISNULL(1) == false)
        hold = interval_to_time_t(PG_GETARG_INTERVAL_P(1));
	
	rc = memcached_delete(globals.mc, key, key_length, hold);
	
    if (rc != MEMCACHED_SUCCESS)
        elog(ERROR, "%s ", memcached_strerror(globals.mc, rc));

    PG_RETURN_BOOL(rc == 0);
}

static time_t
interval_to_time_t(Interval *span)
{
    float8 result;

#ifdef HAVE_INT64_TIMESTAMP
    result = span->time / 1000000e0;
#else
    result = span->time;
#endif

    if (span->month != 0)
    {
        result += (365.25 * 86400) * (span->month / 12);
        result += (30.0 * 86400) * (span->month % 12);
    }

    return (time_t) result;
}

Datum
memcache_flush_all0(PG_FUNCTION_ARGS)
{
	static time_t opt_expire = 0;
    memcached_return rc;

	rc = memcached_flush(globals.mc, opt_expire);
	if (rc != MEMCACHED_SUCCESS) 
		elog(ERROR, "%s", memcached_strerror(globals.mc, rc));

    PG_RETURN_BOOL(rc == 0);
}

Datum
memcache_get(PG_FUNCTION_ARGS)
{
    text *get_key, *ret;
	char *string, *key;
    size_t key_length, return_value_length;
    uint32_t flags;
	memcached_return rc;

    if (PG_ARGISNULL(0))
		elog(ERROR, "memcache key cannot be NULL");

    get_key = PG_GETARG_TEXT_P(0);
	
	key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(get_key)));
    key_length = strlen(key);

	if (key_length < 1)
		elog(ERROR, "memcache key cannot be an empty string");
	if (key_length >= 250)
		elog(ERROR, "memcache key too long");

	string = memcached_get(globals.mc, key, key_length,
						   &return_value_length, &flags, &rc);
	
	if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND)
		elog(ERROR, "%s", memcached_strerror(globals.mc, rc));

	if (rc == MEMCACHED_NOTFOUND)
		PG_RETURN_NULL();

    ret = (text *) palloc(return_value_length + VARHDRSZ);
    SET_VARSIZE(ret, return_value_length + VARHDRSZ);
    memcpy(VARDATA(ret), string, return_value_length);

    PG_RETURN_TEXT_P(ret);
}
 
Datum
memcache_incr(PG_FUNCTION_ARGS)
{
    return memcache_atomic_op(true, fcinfo);
}

Datum
memcache_replace(PG_FUNCTION_ARGS)
{
	return memcache_set_cmd(PG_MEMCACHE_REPLACE | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum
memcache_replace_absexpire(PG_FUNCTION_ARGS)
{
    return memcache_set_cmd(PG_MEMCACHE_REPLACE | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

Datum
memcache_set(PG_FUNCTION_ARGS)
{
	return memcache_set_cmd(PG_MEMCACHE_SET | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum
memcache_set_absexpire(PG_FUNCTION_ARGS)
{
    return memcache_set_cmd(PG_MEMCACHE_SET | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

static Datum
memcache_set_cmd(int type, PG_FUNCTION_ARGS)
{
    text *key, *val;
    size_t key_length, val_length;
    time_t expire;
    TimestampTz timestamptz;
    struct pg_tm tm;
    fsec_t fsec;
    bool ret;

    if (PG_ARGISNULL(0))
        elog(ERROR, "memcache key cannot be NULL");
	if (PG_ARGISNULL(1))
		elog(ERROR, "memcache value cannot be NULL");

    key = PG_GETARG_TEXT_P(0);
    key_length = VARSIZE(key) - VARHDRSZ;
	/* These aren't really needed as we set libmemcached behavior to check for all invalid sets */
    if (key_length < 1)
		elog(ERROR, "memcache key cannot be an empty string");
	if (key_length >= 250) 
		elog(ERROR, "memcache key too long");

    val = PG_GETARG_TEXT_P(1);
    val_length = VARSIZE(val) - VARHDRSZ;

    expire = (time_t) 0.0;
    if (PG_NARGS() >= 3 && PG_ARGISNULL(2) == false)
    {
        if (type & PG_MEMCACHE_TYPE_INTERVAL)
        {
            Interval *span = PG_GETARG_INTERVAL_P(2);
            expire = interval_to_time_t(span);
        }
        else if (type & PG_MEMCACHE_TYPE_TIMESTAMP)
        {
            timestamptz = PG_GETARG_TIMESTAMPTZ(2);

            /* convert to timestamptz to produce consistent results */
            if (timestamp2tm(timestamptz, NULL, &tm, &fsec, NULL, NULL) !=0)
                ereport(ERROR,
                        (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
                         errmsg("timestamp out of range")));

#ifdef HAVE_INT64_TIMESTAMP
            expire = (time_t) ((timestamptz - SetEpochTimestamp()) / 1000000e0);
#else
            expire = (time_t) timestamptz - SetEpochTimestamp();
#endif
        }
        else
            elog(ERROR, "%s():%s:%u: invalid date type", __FUNCTION__, __FILE__, __LINE__);
    }

    ret = do_memcache_set_cmd(type, VARDATA(key), key_length,
                              VARDATA(val), val_length, expire);

    PG_RETURN_BOOL(ret);
}

static bool
do_memcache_set_cmd(int type, char *key, size_t key_length,
                    char *value, size_t value_length, time_t expiration)
{
	memcached_return rc = 1; /*FIXME GCC Warning hach*/
	
    if (type & PG_MEMCACHE_ADD)
		rc = memcached_add (globals.mc, key, key_length,
							value, value_length, expiration, 0);
	
    else if (type & PG_MEMCACHE_REPLACE)
		rc = memcached_replace (globals.mc, key, key_length,
								value, value_length, expiration, 0);
    else if (type & PG_MEMCACHE_SET)
		rc = memcached_set (globals.mc, key, key_length,
							value, value_length, expiration, 0);
    else
        elog(ERROR, "unknown pgmemcache set command type: %d", type);

	if (rc != MEMCACHED_SUCCESS)
		elog(ERROR, "%s", memcached_strerror(globals.mc, rc));
    return (rc == 0);
}

Datum
memcache_server_add(PG_FUNCTION_ARGS)
{
    text *server = PG_GETARG_TEXT_P(0);
	char * host_str;
	memcached_return rc;
	
	host_str = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(server)));

    rc = do_server_add(host_str);
	if (rc != MEMCACHED_SUCCESS)
		elog(ERROR, "%s", memcached_strerror(globals.mc, rc));
    
	PG_RETURN_BOOL(rc == MEMCACHED_SUCCESS);
}

static memcached_return
do_server_add(char *host_str)
{
	memcached_server_st *servers;
	memcached_return rc;
	MemoryContext old_ctx;
	
	old_ctx = MemoryContextSwitchTo(globals.pg_ctxt);
	
	servers = memcached_servers_parse(host_str);
	rc = memcached_server_push(globals.mc, servers);
	memcached_server_list_free(servers);
    
	MemoryContextSwitchTo(old_ctx);
    
	return rc;
}


Datum
memcache_stats(PG_FUNCTION_ARGS)
{
	StringInfoData buf;
	unsigned int i;
	memcached_server_st *server_list;
	memcached_return rc;
	memcached_stat_st *stat;

	initStringInfo(&buf);

	server_list = memcached_server_list(globals.mc);
	stat = memcached_stat(globals.mc, NULL, &rc);
	
	if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_SOME_ERRORS)
		elog(ERROR, "Failed to communicate with servers %s\n", memcached_strerror(globals.mc, rc));
	
	for (i = 0; i < memcached_server_count(globals.mc); i++)
	{
		char **list, **ptr;
		
		list = memcached_stat_get_keys(globals.mc, &stat[i], &rc);
		if (i != 0)
			appendStringInfo(&buf, "\n");

		appendStringInfo(&buf, "Server: %s (%u)\n", memcached_server_name(globals.mc, server_list[i]),
						 memcached_server_port(globals.mc, server_list[i]));
		for (ptr = list; *ptr; ptr++)
		{
			memcached_return rc;
			char *value = memcached_stat_get_value(globals.mc, &stat[i], *ptr, &rc);
			appendStringInfo(&buf, "%s: %s\n", *ptr, value);
			free(value);
		}
		free(list);
	}
	
	PG_RETURN_DATUM(DirectFunctionCall1(textin, CStringGetDatum(buf.data)));
}

Datum
memcache_stat(PG_FUNCTION_ARGS)
{
    text *stat = PG_GETARG_TEXT_P(0);
	memcached_stat_st *stats;
    char *key, *return_value = NULL;
    size_t key_length;
    StringInfoData buf;
	unsigned int i;
	memcached_return rc;
	
    initStringInfo(&buf);
    key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(stat)));
    key_length = strlen(key);
    if (key_length == 0)
		elog(ERROR, "memcache statistic key cannot be the empty string");
	
	stats = memcached_stat(globals.mc, NULL, &rc);
	if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_SOME_ERRORS)
		elog(ERROR, "Failed to communicate with servers %s\n", memcached_strerror(globals.mc, rc));
	
	for (i = 0; i < memcached_server_count(globals.mc); i++)
		return_value = memcached_stat_get_value(globals.mc, &stats[i], key, &rc);

		if (rc != MEMCACHED_SUCCESS)
			elog(ERROR, "%s", memcached_strerror(globals.mc, rc));
	
		if (return_value)
			appendStringInfo(&buf, "%s: %s", key, return_value);
			free(return_value);

    PG_RETURN_DATUM(DirectFunctionCall1(textin, CStringGetDatum(buf.data)));
}

/*
 NOTICE!!
 
 libmemcached doesn't currently provide a public API to do these as of now (version 0.25, Dec 28 2008)
 This pretty much jinxes the idea of reimplementing them on top of libmemcached for now. The code has been left here
 for historical purposes in case someone sees a need to add them in again at a later date.
 
 */

#if 0
Datum
memcache_server_remove(PG_FUNCTION_ARGS)
{
    text                   *host = PG_GETARG_TEXT_P(0);
    char                   *host_str;
    char                   *port_str;
    memcached_server_st       *serv;
	
    host_str = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(host)));
	
    /* If no port specified, use the libmemcache default (11211) */
    if (PG_NARGS() == 1)
        port_str = "11211";
    else
    {
        text *port = PG_GETARG_TEXT_P(1);
		
        port_str = DatumGetCString(DirectFunctionCall1(textout,
                                                       PointerGetDatum(port)));
    }
	
    /*
     * Find the first server that matches the specified host and port
     * in the server list, then remove it.
     */
    TAILQ_FOREACH(serv, &(globals.mc->server_list), entries)
    {
        if (strcmp(serv->hostname, host_str) == 0 &&
            strcmp(serv->port, port_str) == 0)
        {
            /*
             * XXX: all we do is disconnect from the server for
             * now. We should probably remove it from the servers list
             * as well, or rename this function.
             */
            mcm_server_disconnect(globals.mc_ctxt, serv);
            PG_RETURN_BOOL(true);
        }
    }
	
    PG_RETURN_BOOL(false);
}

static Datum
form_simple_server_tup(memcached_server_st *server, TupleDesc tdesc)
{
    Datum                   values[2];
    bool                    nulls[2];
	
    values[0] = DirectFunctionCall1(textin, CStringGetDatum(server->hostname));
    values[1] = DirectFunctionCall1(int4in, CStringGetDatum(server->port));
    nulls[0] = nulls[1] = false;
	
    return HeapTupleGetDatum(heap_form_tuple(tdesc, values, nulls));
}

Datum
memcache_server_list(PG_FUNCTION_ARGS)
{
    FuncCallContext *func_ctx;
	
    if (SRF_IS_FIRSTCALL())
    {
        MemoryContext old_ctx;
        TypeFuncClass func_type;
        TupleDesc     result_tdesc;
		
        func_ctx = SRF_FIRSTCALL_INIT();
		
        old_ctx = MemoryContextSwitchTo(func_ctx->multi_call_memory_ctx);
		
        func_type = get_call_result_type(fcinfo, NULL, &result_tdesc);
        if (func_type != TYPEFUNC_COMPOSITE)
            elog(ERROR, "called in unexpected context");
		
        func_ctx->tuple_desc = BlessTupleDesc(result_tdesc);
        func_ctx->max_calls = globals.mc->num_servers;
		
        MemoryContextSwitchTo(old_ctx);
    }
	
    func_ctx = SRF_PERCALL_SETUP();
	
    if (func_ctx->call_cntr < func_ctx->max_calls)
    {
        memcached_server_st *serv;
        HeapTuple               result;
		
        serv = globals.mc->servers[func_ctx->call_cntr];
        result = form_server_tuple(serv, func_ctx->tuple_desc);
        SRF_RETURN_NEXT(func_ctx, HeapTupleGetDatum(result));
    }
	
    SRF_RETURN_DONE(func_ctx);
}

static HeapTuple
form_server_tuple(memcached_server_st *serv, TupleDesc tdesc)
{
    Datum values[3];
    bool  nulls[3];
    char *state_str;
	
    values[0] = DirectFunctionCall1(textin, CStringGetDatum(serv->hostname));
    values[1] = DirectFunctionCall1(int4in, CStringGetDatum(serv->port));
	
    switch (serv->active)
    {
        case 'd':
            state_str = "Down";
            break;
			
        case 'n':
            state_str = "Unknown host";
            break;
			
        case 't':
            state_str = "Unconnected";
            break;
			
        case 'u':
            state_str = "Connected";
            break;
			
        default:
            elog(ERROR, "unrecognized server state: %c", serv->active);
            state_str = NULL;   /* keep compiler quiet */
    }
	
    values[2] = DirectFunctionCall1(textin, CStringGetDatum(state_str));
	
    nulls[0] = nulls[1] = nulls[2] = false;
	
    return heap_form_tuple(tdesc, values, nulls);
}

Datum
memcache_hash(PG_FUNCTION_ARGS)
{
    text *key = PG_GETARG_TEXT_P(0);
	char *hash_key;
    size_t  key_length;
    u_int32_t hash;
	
	hash_key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(key)));
	hash = generate_hash(globals.mc, hash_key, key_length); // non public API from libmemcached
	
    PG_RETURN_UINT32(hash);
}

Datum
memcache_server_find(PG_FUNCTION_ARGS)
{
	text                   *key = PG_GETARG_TEXT_P(0);
	size_t                  key_len;
	u_int32_t               hash;
	memcached_server_st       *server;
	TupleDesc               tdesc;
	TypeFuncClass           func_type;
	
	key_length = VARSIZE(key) - VARHDRSZ;
	/* These aren't really needed as we set libmemcached behavior to check for all invalid sets */
	if (key_length < 1)
		elog(ERROR, "memcache key cannot be an empty string");
	if (key_length >= 250) 
		elog(ERROR, "memcache key too long");
	
	hash = mcm_hash_key(globals.mc_ctxt, VARDATA(key), key_length);
	server = mcm_server_find(globals.mc_ctxt, globals.mc, hash);
	if (server == NULL)
		elog(ERROR, "failed to find memcache server for key");
	
	/*
	 * XXX: the return type of this function is actually invariant, so
	 * it seems a bit unfortunate to invoke get_call_result_type() for
	 * every call, which can be expensive. Can we just hardcode the
	 * expected tdesc?
	 */
	func_type = get_call_result_type(fcinfo, NULL, &tdesc);
	if (func_type != TYPEFUNC_COMPOSITE)
		elog(ERROR, "called in unexpected context");
	
	PG_RETURN_DATUM(form_simple_server_tup(server, tdesc));
}

Datum
memcache_flush(PG_FUNCTION_ARGS)
{
#if 0
	static time_t opt_expire = 0;
    text *host = PG_GETARG_TEXT_P(0);
	char * host_str;
	memcached_server_st *servers;
	memcached_return rc;
	
	host_str = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(host)));
	
	servers = memcached_servers_parse(host_str);
	rc = memcached_flush(servers, opt_expire);
	
	if (rc != MEMCACHED_SUCCESS) 
		elog(ERROR, "%s", memcached_strerror(globals.mc, rc));
	
    PG_RETURN_BOOL(rc == 0);
#endif
}

Datum
memcache_server_find_hash(PG_FUNCTION_ARGS)
{
    uint32                  hash = PG_GETARG_UINT32(0);
    memcached_server_st *server;
    TypeFuncClass           func_type;
    TupleDesc               tdesc;
	
    server = mcm_server_find(globals.mc_ctxt, globals.mc, hash);
    if (server == NULL)
        elog(ERROR, "failed to find memcache server for key");
	
    func_type = get_call_result_type(fcinfo, NULL, &tdesc);
    if (func_type != TYPEFUNC_COMPOSITE)
        elog(ERROR, "called in unexpected context");
	
    PG_RETURN_DATUM(form_simple_server_tup(server, tdesc));
}
#endif
