/*
 * PostgreSQL functions to interface with memcache.
 *
 * Copyright (c) 2004-2005 Sean Chittenden <sean@chittenden.org>
 * Copyright (c) 2007-2008 Neil Conway <neilc@samurai.com>
 * Copyright (c) 2007 Open Technology Group, Inc. <http://www.otg-nc.com>
 * Copyright (c) 2008-2009 Hannu Valtonen <hannu.valtonen@hut.fi>
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
#include "utils/lsyscache.h"

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
static char *memcache_default_servers = "";
static char *memcache_default_behavior = "";
static GucStringAssignHook assign_default_servers_guc(const char *newval,
                                                      bool doit, GucSource source);
static GucStringAssignHook assign_default_behavior_guc (const char *newval,
                                                        bool doit, GucSource source);
static GucStringAssignHook assign_default_behavior (const char *newval);
static GucShowHook show_default_servers_guc (void);
static GucShowHook show_default_behavior_guc (void);
static memcached_behavior get_memcached_behavior_flag (const char *flag);
static uint64_t get_memcached_behavior_data (const char *flag, const char *data);
static uint64_t get_memcached_hash_type (const char *data);
static uint64_t get_memcached_distribution_type (const char *data);
static Datum memcache_atomic_op(bool increment, PG_FUNCTION_ARGS);
static Datum memcache_set_cmd(int type, PG_FUNCTION_ARGS);
static memcached_return do_server_add(char *host_str);
static bool do_memcache_set_cmd(int type, char *key, size_t key_len,
                                char *val, size_t val_len, time_t expire);
static time_t interval_to_time_t(Interval *span);

#define PG_MEMCACHE_ADD                 0x0001
#define PG_MEMCACHE_REPLACE             0x0002
#define PG_MEMCACHE_SET                 0x0004
#define PG_MEMCACHE_PREPEND             0x0008
#define PG_MEMCACHE_APPEND              0x0010
#define PG_MEMCACHE_TYPE_INTERVAL       0x0100
#define PG_MEMCACHE_TYPE_TIMESTAMP      0x0200

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
	
    MemoryContextSwitchTo(old_ctxt);

    DefineCustomStringVariable("pgmemcache.default_servers",
                               "Comma-separated list of memcached servers to connect to.",
                               "Specified as a comma-separated list of host:port (port is optional).",
                               &memcache_default_servers,
#if defined(PG_VERSION_NUM) && (80400 <= PG_VERSION_NUM)
                               NULL,
#endif
                               PGC_USERSET,
#if defined(PG_VERSION_NUM) && (80400 <= PG_VERSION_NUM)
                               GUC_LIST_INPUT,
#endif
                               (GucStringAssignHook) assign_default_servers_guc,
                               (GucShowHook) show_default_servers_guc);
    
    DefineCustomStringVariable ("pgmemcache.default_behavior",
                                "Comma-separated list of memcached behavior (optional).",
                                "Specified as a comma-separated list of behavior_flag:behavior_data.",
                                &memcache_default_behavior,
#if defined(PG_VERSION_NUM) && (80400 <= PG_VERSION_NUM)
                                NULL,
#endif
                                PGC_USERSET,
#if defined(PG_VERSION_NUM) && (80400 <= PG_VERSION_NUM)
                                GUC_LIST_INPUT,
#endif
                                (GucStringAssignHook) assign_default_behavior_guc,
                                (GucShowHook) show_default_behavior_guc);
}


static GucStringAssignHook
assign_default_servers_guc(const char *newval, bool doit, GucSource source)
{
    do_server_add((char *) newval);
    return (GucStringAssignHook) newval;
}

static GucShowHook
show_default_servers_guc(void)
{
    return (GucShowHook) memcache_default_servers ? (GucShowHook) memcache_default_servers: (GucShowHook)"";
}

static GucStringAssignHook
assign_default_behavior_guc (const char *newval, bool doit, GucSource source)
{
    return assign_default_behavior (newval);
}

static GucShowHook
show_default_behavior_guc (void)
{
    return (GucShowHook) memcache_default_behavior ? (GucShowHook) memcache_default_servers: (GucShowHook) "";
}

static GucStringAssignHook
assign_default_behavior (const char *newval)
{
    int i, len;
    StringInfoData flag_buf;
    StringInfoData data_buf;
    memcached_return rc;
    MemoryContext old_ctx;
    old_ctx = MemoryContextSwitchTo(globals.pg_ctxt);

    initStringInfo (&flag_buf);
    initStringInfo (&data_buf);

    len = strlen (newval);

    for (i = 0; i < len; i++) 
    {
        char c = newval[i];

        if (c == ',' || c == ':') 
        {
            if (flag_buf.len == 0)
                return NULL;

            if (c == ':') {
                int j;
                for (j = i + 1; j < len; j++)
                {
                    if (newval[j] == ',')
                        break;
                    appendStringInfoChar (&data_buf, newval[j]);
                }

                if (data_buf.len == 0)
                    return NULL;

                i += data_buf.len;
            }
            rc = memcached_behavior_set (globals.mc,
                                         get_memcached_behavior_flag
                                         (flag_buf.data),
                                         get_memcached_behavior_data
                                         (flag_buf.data, data_buf.data));

            /* Skip the element separator, reset buffers */
            i++;
            flag_buf.data[0] = '\0';
            flag_buf.len = 0;
            data_buf.data[0] = '\0';
            data_buf.len = 0;
        }
        else 
        {
            appendStringInfoChar (&flag_buf, c);
        }
    }
    pfree (flag_buf.data);
    pfree (data_buf.data);

    MemoryContextSwitchTo(old_ctx);

    return (GucStringAssignHook) newval;
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
	
    if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND)
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
    
    string = memcached_get(globals.mc, key, key_length, &return_value_length, &flags, &rc);
    
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
memcache_get_multi(PG_FUNCTION_ARGS)
{
    ArrayType  *array;
    int array_length, array_lbound, i;
    Oid element_type;
    uint32_t flags;
    memcached_return rc;
    char typalign;
    int16 typlen;
    bool typbyval;    
    char **keys, *value;
    size_t *key_lens, value_length;
    FuncCallContext *funcctx;
    MemoryContext oldcontext;
    internal_fctx *fctx;
    TupleDesc            tupdesc;
    AttInMetadata       *attinmeta;

    if (PG_ARGISNULL(0))
      elog(ERROR, "memcache get_multi key cannot be null");
    
    array = PG_GETARG_ARRAYTYPE_P(0);
    if (ARR_NDIM(array) != 1)
      elog(ERROR, "pgmemcache only supports single dimension ARRAYs, not: ARRAYs with %d dimensions", ARR_NDIM(array));
    
    array_lbound = ARR_LBOUND(array)[0];
    array_length = ARR_DIMS(array)[0];
    element_type = ARR_ELEMTYPE(array);

    if (SRF_IS_FIRSTCALL())
    {
                /* create a function context for cross-call persistence */
                funcctx = SRF_FIRSTCALL_INIT();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
                funcctx->max_calls = array_length;
		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		  ereport(ERROR,
                    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                     errmsg("function returning record called in context that cannot accept type record")));
                fctx = (internal_fctx *) palloc(sizeof(internal_fctx));

		get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);
    
		keys = palloc(sizeof(char *) * array_length);
		key_lens = palloc(sizeof(size_t) * array_length);
	       
		for (i = 0;i < array_length;i++) {
		  int offset = array_lbound + i;
		  bool isnull;
		  Datum elem;
		  
		  elem = array_ref(array, 1, &offset, 0, typlen, typbyval, typalign, &isnull);
		  if(!isnull) {
		    keys[i] = TextDatumGetCString(PointerGetDatum(elem));
		    key_lens[i] = strlen(keys[i]);
		  }
		}
		fctx->keys = keys;
		fctx->key_lens = key_lens;

		rc = memcached_mget(globals.mc, (const char **)keys, key_lens, array_length);
		if (rc != MEMCACHED_SUCCESS)
		  elog(ERROR, "%s", memcached_strerror(globals.mc, rc));
		
		if (rc == MEMCACHED_NOTFOUND)
		  PG_RETURN_NULL();
		
		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;
		funcctx->user_fctx = fctx;
                MemoryContextSwitchTo(oldcontext);
    }
    
    funcctx = SRF_PERCALL_SETUP();
    fctx = funcctx->user_fctx;
    attinmeta = funcctx->attinmeta;

    if ((value = memcached_fetch(globals.mc, fctx->keys[funcctx->call_cntr], &fctx->key_lens[funcctx->call_cntr], &value_length, &flags, &rc)) != NULL) {
      char       **values;
      HeapTuple    tuple;
      Datum        result;

      if (value == NULL && rc == MEMCACHED_END) 
	SRF_RETURN_DONE(funcctx);
      else if (rc != MEMCACHED_SUCCESS) {
	elog(ERROR, "%s", memcached_strerror(globals.mc, rc));
	SRF_RETURN_DONE(funcctx);
      }
      values = (char **) palloc(2 * sizeof(char *));
      values[0] = (char *) palloc(fctx->key_lens[funcctx->call_cntr] * sizeof(char));
      values[1] = (char *) palloc(value_length * sizeof(char));

      snprintf(values[0], fctx->key_lens[funcctx->call_cntr] + 1, "%s", fctx->keys[funcctx->call_cntr]);
      snprintf(values[1], value_length + 1, "%s", value);

      tuple = BuildTupleFromCStrings(attinmeta, values);
      result = HeapTupleGetDatum(tuple);

      SRF_RETURN_NEXT(funcctx, result);
    }
    SRF_RETURN_DONE(funcctx);
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

Datum
memcache_prepend(PG_FUNCTION_ARGS)
{
	return memcache_set_cmd(PG_MEMCACHE_PREPEND | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum
memcache_prepend_absexpire(PG_FUNCTION_ARGS)
{
    return memcache_set_cmd(PG_MEMCACHE_PREPEND | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

Datum
memcache_append(PG_FUNCTION_ARGS)
{
	return memcache_set_cmd(PG_MEMCACHE_APPEND | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum
memcache_append_absexpire(PG_FUNCTION_ARGS)
{
    return memcache_set_cmd(PG_MEMCACHE_APPEND | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

static Datum
memcache_set_cmd(int type, PG_FUNCTION_ARGS)
{
    text *key = NULL, *val;
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

    ret = do_memcache_set_cmd(type, VARDATA(key), key_length, VARDATA(val), val_length, expire);

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
    else if (type & PG_MEMCACHE_PREPEND)
		rc = memcached_prepend (globals.mc, key, key_length,
							value, value_length, expiration, 0);
    else if (type & PG_MEMCACHE_APPEND)
		rc = memcached_append (globals.mc, key, key_length,
							value, value_length, expiration, 0);
    else if (type & PG_MEMCACHE_PREPEND)
		rc = memcached_prepend (globals.mc, key, key_length,
							value, value_length, expiration, 0);
    else if (type & PG_MEMCACHE_APPEND)
		rc = memcached_append (globals.mc, key, key_length,
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

static memcached_behavior
get_memcached_behavior_flag (const char *flag)
{
    memcached_behavior ret = -1;

    /*Sort by flag in reverse order. */
    if (strncmp ("MEMCACHED_BEHAVIOR_HASH_WITH_PREFIX_KEY", flag, 39) == 0 || strncmp ("HASH_WITH_PREFIX_KEY", flag, 21) == 0)
        ret = MEMCACHED_BEHAVIOR_HASH_WITH_PREFIX_KEY;
    else if (strncmp ("MEMCACHED_BEHAVIOR_VERIFY_KEY", flag, 29) == 0 || strncmp ("VERIFY_KEY", flag, 10) == 0)
        ret = MEMCACHED_BEHAVIOR_VERIFY_KEY;
    else if (strncmp ("MEMCACHED_BEHAVIOR_USER_DATA", flag, 28) == 0 || strncmp ("USER_DATA", flag, 9) == 0)
        ret = MEMCACHED_BEHAVIOR_USER_DATA;
    else if (strncmp ("MEMCACHED_BEHAVIOR_TCP_NODELAY", flag, 30) == 0 || strncmp ("TCP_NODELAY", flag, 11) == 0)
        ret = MEMCACHED_BEHAVIOR_TCP_NODELAY;
    else if (strncmp ("MEMCACHED_BEHAVIOR_SUPPORT_CAS", flag, 30) == 0 || strncmp ("SUPPORT_CAS", flag, 11) == 0)
        ret = MEMCACHED_BEHAVIOR_SUPPORT_CAS;
    else if (strncmp ("MEMCACHED_BEHAVIOR_SORT_HOSTS", flag, 29) == 0 || strncmp ("SORT_HOSTS", flag, 10) == 0)
        ret = MEMCACHED_BEHAVIOR_SORT_HOSTS;
    else if (strncmp ("MEMCACHED_BEHAVIOR_SOCKET_SEND_SIZE", flag, 35) == 0 || strncmp ("SOCKET_SEND_SIZE", flag, 16) == 0)
        ret = MEMCACHED_BEHAVIOR_SOCKET_SEND_SIZE;
    else if (strncmp ("MEMCACHED_BEHAVIOR_SOCKET_RECV_SIZE", flag, 35) == 0 || strncmp ("SOCKET_RECV_SIZE", flag, 16) == 0)
        ret = MEMCACHED_BEHAVIOR_SOCKET_RECV_SIZE;
    else if (strncmp ("MEMCACHED_BEHAVIOR_SND_TIMEOUT", flag, 30) == 0 || strncmp ("SND_TIMEOUT", flag, 11) == 0)
        ret = MEMCACHED_BEHAVIOR_SND_TIMEOUT;
    else if (strncmp ("MEMCACHED_BEHAVIOR_SERVER_FAILURE_LIMIT", flag, 39) == 0 || strncmp ("SERVER_FAILURE_LIMIT", flag, 20) == 0)
        ret = MEMCACHED_BEHAVIOR_SERVER_FAILURE_LIMIT;
    else if (strncmp ("MEMCACHED_BEHAVIOR_RETRY_TIMEOUT", flag, 32) == 0 || strncmp ("RETRY_TIMEOUT", flag, 13) == 0)
        ret = MEMCACHED_BEHAVIOR_RETRY_TIMEOUT;
    else if (strncmp ("MEMCACHED_BEHAVIOR_RCV_TIMEOUT", flag, 30) == 0 || strncmp ("RCV_TIMEOUT", flag, 11) == 0)
        ret = MEMCACHED_BEHAVIOR_RCV_TIMEOUT;
    else if (strncmp ("MEMCACHED_BEHAVIOR_POLL_TIMEOUT", flag, 31) == 0 || strncmp ("POLL_TIMEOUT", flag, 12) == 0)
        ret = MEMCACHED_BEHAVIOR_POLL_TIMEOUT;
    else if (strncmp ("MEMCACHED_BEHAVIOR_NO_BLOCK", flag, 27) == 0 || strncmp ("NO_BLOCK", flag, 8) == 0)
        ret = MEMCACHED_BEHAVIOR_NO_BLOCK;
    else if (strncmp ("MEMCACHED_BEHAVIOR_KETAMA_WEIGHTED", flag, 34) == 0 || strncmp ("KETAMA_WEIGHTED", flag, 15) == 0)
        ret = MEMCACHED_BEHAVIOR_KETAMA_WEIGHTED;
    else if (strncmp ("MEMCACHED_BEHAVIOR_KETAMA_HASH", flag, 30) == 0 || strncmp ("KETAMA_HASH", flag, 11) == 0)
        ret = MEMCACHED_BEHAVIOR_KETAMA_HASH;
    else if (strncmp ("MEMCACHED_BEHAVIOR_KETAMA", flag, 25) == 0 || strncmp ("KETAMA", flag, 6) == 0)
        ret = MEMCACHED_BEHAVIOR_KETAMA;
    else if (strncmp ("MEMCACHED_BEHAVIOR_IO_MSG_WATERMARK", flag, 35) == 0 || strncmp ("IO_MSG_WATERMARK", flag, 16) == 0)
        ret = MEMCACHED_BEHAVIOR_IO_MSG_WATERMARK;
    else if (strncmp ("MEMCACHED_BEHAVIOR_IO_BYTES_WATERMARK", flag, 37) == 0 || strncmp ("IO_BYTES_WATERMARK", flag, 18) == 0)
        ret = MEMCACHED_BEHAVIOR_IO_BYTES_WATERMARK;
    else if (strncmp ("MEMCACHED_BEHAVIOR_HASH", flag, 23) == 0 || strncmp ("HASH", flag, 4) == 0)
        ret = MEMCACHED_BEHAVIOR_HASH;
    else if (strncmp ("MEMCACHED_BEHAVIOR_DISTRIBUTION", flag, 31) == 0 || strncmp ("DISTRIBUTION", flag, 12) == 0)
        ret = MEMCACHED_BEHAVIOR_DISTRIBUTION;
    else if (strncmp ("MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT", flag, 34) == 0 || strncmp ("CONNECT_TIMEOUT", flag, 15) == 0)
        ret = MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT;
    else if (strncmp ("MEMCACHED_BEHAVIOR_CACHE_LOOKUPS", flag, 33) == 0 || strncmp ("CACHE_LOOKUPS", flag, 14) == 0)
        ret = MEMCACHED_BEHAVIOR_CACHE_LOOKUPS;
    else if (strncmp ("MEMCACHED_BEHAVIOR_BUFFER_REQUESTS", flag, 34) == 0 || strncmp ("BUFFER_REQUESTS", flag, 15) == 0)
        ret = MEMCACHED_BEHAVIOR_BUFFER_REQUESTS;
    else if (strncmp ("MEMCACHED_BEHAVIOR_BINARY_PROTOCOL", flag, 34) == 0 || strncmp ("BINARY_PROTOCOL", flag, 15) == 0)
        ret = MEMCACHED_BEHAVIOR_BINARY_PROTOCOL;
    else
        elog (ERROR, "unknown memcached behavior flag: %s", flag);

    return ret;
}

static uint64_t
get_memcached_behavior_data (const char *flag, const char *data)
{
    char *endptr;
    memcached_behavior f_code = get_memcached_behavior_flag (flag);
    uint64_t ret;

    switch (f_code) 
    {
        case MEMCACHED_BEHAVIOR_HASH:
        case MEMCACHED_BEHAVIOR_KETAMA_HASH:
            ret = get_memcached_hash_type (data);
            break;
        case MEMCACHED_BEHAVIOR_DISTRIBUTION:
            ret = get_memcached_distribution_type (data);
            break;
        default:
            ret = strtol (data, &endptr, 10);
            if (endptr == data)
                elog (ERROR, "invalid memcached behavior param %s: %s", flag, data);
    }
    
    return ret;
}

static uint64_t
get_memcached_hash_type (const char *data)
{
    uint64_t ret;

    /* Sort by data in reverse order. */
    if (strncmp ("MEMCACHED_HASH_MURMUR", data, 21) == 0 || strncmp ("MURMUR", data, 6) == 0)
        ret = MEMCACHED_HASH_MURMUR;
    else if (strncmp ("MEMCACHED_HASH_MD5", data, 18) == 0 || strncmp ("MD5", data, 3) == 0)
        ret = MEMCACHED_HASH_MD5;
    else if (strncmp ("MEMCACHED_HASH_JENKINS", data, 22) == 0 || strncmp ("JENKINS", data, 7) == 0)
        ret = MEMCACHED_HASH_JENKINS;
    else if (strncmp ("MEMCACHED_HASH_HSIEH", data, 20) == 0 || strncmp ("HSIEH", data, 5) == 0)
        ret = MEMCACHED_HASH_HSIEH;
    else if (strncmp ("MEMCACHED_HASH_FNV1A_64", data, 23) == 0 || strncmp ("FNV1A_64", data, 8) == 0)
        ret = MEMCACHED_HASH_FNV1A_64;
    else if (strncmp ("MEMCACHED_HASH_FNV1A_32", data, 23) == 0 || strncmp ("FNV1A_32", data, 8) == 0)
        ret = MEMCACHED_HASH_FNV1A_32;
    else if (strncmp ("MEMCACHED_HASH_FNV1_64", data, 22) == 0 || strncmp ("FNV1_64", data, 7) == 0)
        ret = MEMCACHED_HASH_FNV1_64;
    else if (strncmp ("MEMCACHED_HASH_FNV1_32", data, 22) == 0 || strncmp ("FNV1_32", data, 7) == 0)
        ret = MEMCACHED_HASH_FNV1_32;
    else if (strncmp ("MEMCACHED_HASH_DEFAULT", data, 22) == 0 || strncmp ("DEFAULT", data, 7) == 0)
        ret = MEMCACHED_HASH_DEFAULT;
    else if (strncmp ("MEMCACHED_HASH_CRC", data, 18) == 0 || strncmp ("CRC", data, 3) == 0)
        ret = MEMCACHED_HASH_CRC;
    else 
    {
        ret = 0xffffffff; /* to avoid warning */
        elog (ERROR, "invalid hash name: %s", data);
    }
    
    return ret;
}

static uint64_t
get_memcached_distribution_type (const char *data)
{
    uint64_t ret;

    /* Sort by data in reverse order. */
    if (strncmp ("MEMCACHED_DISTRIBUTION_RANDOM", data, 29) == 0 || strncmp ("RANDOM", data, 6) == 0)
        ret = MEMCACHED_DISTRIBUTION_RANDOM;
    else if (strncmp ("MEMCACHED_DISTRIBUTION_MODULA", data, 29) == 0 || strncmp ("MODULA", data, 6) == 0)
        ret = MEMCACHED_DISTRIBUTION_MODULA;
    else if (strncmp ("MEMCACHED_DISTRIBUTION_CONSISTENT_KETAMA", data, 40) == 0 || strncmp ("CONSISTENT_KETAMA", data, 17) == 0)
        ret = MEMCACHED_DISTRIBUTION_CONSISTENT_KETAMA;
    else if (strncmp ("MEMCACHED_DISTRIBUTION_CONSISTENT", data, 33) == 0 || strncmp ("CONSISTENT", data, 10) == 0)
        ret = MEMCACHED_DISTRIBUTION_CONSISTENT;
    else 
    {
        ret = 0xffffffff; /* to avoid warning */
        elog (ERROR, "invalid distribution name: %s", data);
    }

    return ret;
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
