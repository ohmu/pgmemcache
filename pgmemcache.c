/*
 * PostgreSQL functions to interface with memcache.
 *
 * Copyright (c) 2004-2005 Sean Chittenden <sean@chittenden.org>
 * Copyright (c) 2007-2008 Neil Conway <neilc@samurai.com>
 * Copyright (c) 2007 Open Technology Group, Inc. <http://www.otg-nc.com>
 * Copyright (c) 2008-2013 Hannu Valtonen <hannu.valtonen@ohmu.fi>
 * Copyright (c) 2012-2014 Ohmu Ltd <opensource@ohmu.fi>
 *
 * See the file LICENSE for distribution terms.
 */

#ifdef USE_OMCACHE
#define OMCACHE_READ_TIMEOUT 2000
#include "omcache_libmemcached.h"
#include <syslog.h>  /* for log levels */
#endif /* USE_OMCACHE */
#ifdef USE_LIBMEMCACHED
#include <libmemcached/memcached.h>
#endif /* USE_LIBMEMCACHED */

#include "pgmemcache.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/* Internal functions */
static void pgmemcache_reset_context(void);
static void pgmemcache_xact_callback(XactEvent event, void *arg);
static void assign_sasl_params(const char *username, const char *password);
static void assign_default_servers_guc(const char *newval, void *extra);
static void assign_default_behavior_guc(const char *newval, void *extra);
static memcached_behavior get_memcached_behavior_flag(const char *flag);
static uint64_t get_memcached_behavior_data(const char *flag, const char *data, const char **val);
static Datum memcache_set_cmd(int type, PG_FUNCTION_ARGS);
static memcached_return do_server_add(const char *host_str);

/* Per-backend global state. */
static struct memcache_global_s
{
  memcached_st *mc;
  bool flush_needed;
  bool flush_on_commit;
  char *default_servers;
  char *default_behavior;
  char *sasl_authentication_username;
  char *sasl_authentication_password;
} globals;


void _PG_init(void)
{
  pgmemcache_reset_context();

  DefineCustomStringVariable("pgmemcache.default_servers",
                             "Comma-separated list of memcached servers to connect to.",
                             "Specified as a comma-separated list of host:port (port is optional).",
                             &globals.default_servers,
                             NULL,
                             PGC_USERSET,
                             GUC_LIST_INPUT,
#if defined(PG_VERSION_NUM) && (PG_VERSION_NUM >= 90100)
                             NULL,
#endif
                             assign_default_servers_guc,
                             NULL);

  DefineCustomStringVariable("pgmemcache.default_behavior",
                             "Comma-separated list of memcached behavior (optional).",
                             "Specified as a comma-separated list of behavior_flag:behavior_data.",
                             &globals.default_behavior,
                             NULL,
                             PGC_USERSET,
                             GUC_LIST_INPUT,
#if defined(PG_VERSION_NUM) && (PG_VERSION_NUM >= 90100)
                             NULL,
#endif
                             assign_default_behavior_guc,
                             NULL);

  DefineCustomBoolVariable("pgmemcache.flush_on_commit",
                           "Whether to flush all buffers to memcached on end of commit",
                           NULL,
                           &globals.flush_on_commit,
                           false,
                           PGC_USERSET,
                           0,
#if defined(PG_VERSION_NUM) && (PG_VERSION_NUM >= 90100)
                           NULL,
#endif
                           NULL,
                           NULL);

  DefineCustomStringVariable("pgmemcache.sasl_authentication_username",
                             "pgmemcache SASL user authentication username",
                             "Simple string pgmemcache.sasl_authentication_username = 'testing_username'",
                             &globals.sasl_authentication_username,
                             NULL,
                             PGC_USERSET,
                             GUC_LIST_INPUT,
#if defined(PG_VERSION_NUM) && (PG_VERSION_NUM >= 90100)
                             NULL,
#endif
                             NULL,
                             NULL);

  DefineCustomStringVariable("pgmemcache.sasl_authentication_password",
                             "pgmemcache SASL user authentication password",
                             "Simple string pgmemcache.sasl_authentication_password = 'testing_password'",
                             &globals.sasl_authentication_password,
                             NULL,
                             PGC_USERSET,
                             GUC_LIST_INPUT,
#if defined(PG_VERSION_NUM) && (PG_VERSION_NUM >= 90100)
                             NULL,
#endif
                             NULL,
                             NULL);

  /* XXX: We should have real assign_hook for these so that memcache sasl data
   * is updated when the values are changed.  */
  assign_sasl_params(globals.sasl_authentication_username, globals.sasl_authentication_password);

  RegisterXactCallback(pgmemcache_xact_callback, NULL);
}

/* This is called when we're being unloaded from a process. Note that
 * this only happens when we're being replaced by a LOAD (e.g. it
 * doesn't happen on process exit), so we can't depend on it being
 * called. */
void _PG_fini(void)
{
  memcached_free(globals.mc);
}

static time_t interval_to_time_t(Interval *span)
{
  float8 result;

#ifdef HAVE_INT64_TIMESTAMP
  result = span->time / 1000000e0;
#else
  result = span->time;
#endif

  result += span->day * 86400;

  if (span->month != 0)
    {
      result += (365.25 * 86400) * (span->month / 12);
      result += (30.0 * 86400) * (span->month % 12);
    }

  return (time_t) result;
}

/* called at end of transaction, flush all buffers to memcache */
static void pgmemcache_xact_callback(XactEvent event, void *arg)
{
  if ((event == XACT_EVENT_COMMIT || event == XACT_EVENT_PRE_COMMIT) &&
      globals.flush_on_commit && globals.flush_needed)
    {
#ifdef USE_LIBMEMCACHED
      memcached_return rc = memcached_flush_buffers(globals.mc);
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
      int rc = omcache_io(globals.mc, NULL, NULL, NULL, NULL, OMCACHE_READ_TIMEOUT);
#endif /* USE_OMCACHE */
      if (rc != MEMCACHED_SUCCESS)
        elog(WARNING, "pgmemcache: memcached_flush_buffers: %s",
                      memcached_strerror(globals.mc, rc));
      else
        globals.flush_needed = false;
    }
}

#ifdef USE_OMCACHE
static void pgmemcache_log_func(void *context, int syslog_level, const char *msg)
{
  int pg_level = LOG_INFO;
  switch (syslog_level)
    {
    case LOG_ERR:
    case LOG_WARNING: pg_level = WARNING; break;
    case LOG_NOTICE: pg_level = NOTICE; break;
    }
  elog(pg_level, "%s", msg);
}
#endif /* USE_OMCACHE */

static void pgmemcache_reset_context(void)
{
  if (globals.mc)
    {
      memcached_free(globals.mc);
      globals.mc = NULL;
    }

  globals.mc = memcached_create(NULL);

#ifdef USE_OMCACHE
  omcache_set_log_callback(globals.mc, 0, pgmemcache_log_func, NULL);
#endif /* USE_OMCACHE */

#ifdef USE_LIBMEMCACHED
  /* Always use the memcache binary protocol as required for
     memcached_(increment|decrement)_with_initial. */
  {
    int rc = memcached_behavior_set(globals.mc,
                                    MEMCACHED_BEHAVIOR_BINARY_PROTOCOL,
                                    1);
    if (rc != MEMCACHED_SUCCESS)
      elog(WARNING, "pgmemcache: memcached_behavior_set(BINARY_PROTOCOL, 1): %s",
                    memcached_strerror(globals.mc, rc));
  }
#endif /* USE_LIBMEMCACHED */

  assign_default_behavior_guc(globals.default_behavior, NULL);
  assign_sasl_params(globals.sasl_authentication_username, globals.sasl_authentication_password);
}

static void assign_sasl_params(const char *username, const char *password)
{
#if LIBMEMCACHED_WITH_SASL_SUPPORT
  if (username != NULL && strlen(username) > 0 && password != NULL && strlen(password) > 0)
    {
      int rc = memcached_set_sasl_auth_data(globals.mc, username, password);
      if (rc != MEMCACHED_SUCCESS)
        elog(ERROR, "pgmemcache: memcached_set_sasl_auth_data: %s",
                    memcached_strerror(globals.mc, rc));
      rc = sasl_client_init(NULL);
      if (rc != SASL_OK)
        elog(ERROR, "pgmemcache: sasl_client_init failed: %d", rc);
    }
#endif
}

static void assign_default_servers_guc(const char *newval, void *extra)
{
  if (newval)
    {
#ifdef USE_LIBMEMCACHED
      /* there is no way to remove servers from a memcache context, so
       * recreate it from scratch when the server list changes */
      pgmemcache_reset_context();
#endif /* USE_LIBMEMCACHED */
      do_server_add(newval);
    }
}

static void assign_default_behavior_guc(const char *newval, void *extra)
{
  int i, len;
  StringInfoData flag_buf;
  StringInfoData data_buf;
  memcached_return rc;
  memcached_behavior bkey;
  uint64_t bval;
  const char *bvalstr = "";

  if (!newval)
    return;

  initStringInfo(&flag_buf);
  initStringInfo(&data_buf);

  len = strlen(newval);

  for (i = 0; i < len; i++)
    {
      char c = newval[i];

      if (c == ',' || c == ':')
        {
          if (flag_buf.len == 0)
            return;

          if (c == ':')
            {
              int j;
              for (j = i + 1; j < len; j++)
                {
                  if (newval[j] == ',')
                    break;
                  appendStringInfoChar(&data_buf, newval[j]);
                }

              if (data_buf.len == 0)
                return;

              i += data_buf.len;
            }
          rc = MEMCACHED_FAILURE;
          bkey = get_memcached_behavior_flag(flag_buf.data);
          bval = get_memcached_behavior_data(flag_buf.data, data_buf.data, &bvalstr);
#ifdef USE_OMCACHE
          if (bkey == NULL)
            rc = OMCACHE_FAIL;
          else if (strcmp(bkey, "BINARY_PROTOCOL") == 0)
            {
              if (!bval)
                elog(ERROR, "pgmemcache: omcache always uses binary protocol");
              rc = OMCACHE_OK;
            }
          else if (strcmp(bkey, "BUFFER_REQUESTS") == 0)
            rc = omcache_set_buffering(globals.mc, bval);
          else if (strcmp(bkey, "CONNECT_TIMEOUT") == 0)
            rc = omcache_set_connect_timeout(globals.mc, bval);
          else if (strcmp(bkey, "DEAD_TIMEOUT") == 0)
            rc = omcache_set_dead_timeout(globals.mc, bval * 1000);
          else if (strcmp(bkey, "DISTRIBUTION") == 0)
            {
              if (strcmp(bvalstr, "CONSISTENT") && strcmp(bvalstr, "CONSISTENT_KETAMA"))
                elog(ERROR, "pgmemcache: omcache always uses ketama");
              rc = OMCACHE_OK;
            }
          else if (strcmp(bkey, "HASH") == 0 || strcmp(bkey, "KETAMA_HASH") == 0)
            {
              if (strcmp(bvalstr, "DEFAULT"))
                elog(ERROR, "pgmemcache: omcache always uses the 'default' (bob jenkins' 'one at a time') hash");
              rc = OMCACHE_OK;
            }
          else if (strcmp(bkey, "KETAMA") == 0)
            {
              if (!bval)
                elog(ERROR, "pgmemcache: omcache always uses a ketama distribution method");
              rc = omcache_set_distribution_method(globals.mc, &omcache_dist_libmemcached_ketama);
            }
          else if (strcmp(bkey, "KETAMA_WEIGHTED") == 0)
            {
              if (!bval)
                elog(ERROR, "pgmemcache: omcache always uses a ketama distribution method");
              rc = omcache_set_distribution_method(globals.mc, &omcache_dist_libmemcached_ketama_weighted);
            }
          else if (strcmp(bkey, "KETAMA_PRE1010") == 0)
            {
              if (!bval)
                elog(ERROR, "pgmemcache: omcache always uses a ketama distribution method");
              rc = omcache_set_distribution_method(globals.mc, &omcache_dist_libmemcached_ketama_pre1010);
            }
          else if (strcmp(bkey, "NO_BLOCK") == 0)
            rc = OMCACHE_OK;  // omcache is non-blocking by default
          else if (strcmp(bkey, "NOREPLY") == 0)
            rc = OMCACHE_OK;  // this isn't really a behavior in omcache
          else if (strcmp(bkey, "REMOVE_FAILED_SERVERS") == 0)
            rc = OMCACHE_OK;  // omcache doesn't have this concept
          else if (strcmp(bkey, "RETRY_TIMEOUT") == 0)
            rc = omcache_set_reconnect_timeout(globals.mc, bval * 1000);
          else if (strcmp(bkey, "SUPPORT_CAS") == 0)
            rc = OMCACHE_OK;  // omcache uses binary protocol which always has cas
          else
            {
              elog(ERROR, "pgmemcache: unsupported behavior %s for omcache", flag_buf.data);
            }
#endif /* USE_OMCACHE */
#ifdef USE_LIBMEMCACHED
          rc = memcached_behavior_set(globals.mc, bkey, bval);
#endif /* USE_LIBMEMCACHED */
          if (rc != MEMCACHED_SUCCESS)
            elog(WARNING, "pgmemcache: memcached_behavior_set: %s",
                          memcached_strerror(globals.mc, rc));
          /* Skip the element separator, reset buffers */
          i++;
          flag_buf.data[0] = '\0';
          flag_buf.len = 0;
          data_buf.data[0] = '\0';
          data_buf.len = 0;
        }
      else
        {
          appendStringInfoChar(&flag_buf, c);
        }
    }
  pfree(flag_buf.data);
  pfree(data_buf.data);
}

Datum memcache_add(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_ADD | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum memcache_add_absexpire(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_ADD | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

static Datum memcache_delta_op(bool increment, PG_FUNCTION_ARGS)
{
  text *atomic_key = PG_GETARG_TEXT_P(0);
  char *key;
  size_t key_length;
  uint64_t val;
  int64_t offset = 1;
  memcached_return rc;

  key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(atomic_key)));
  key_length = strlen(key);

  if (key_length < 1)
    elog(ERROR, "pgmemcache: key cannot be an empty string");
  if (key_length >= 250)
    elog(ERROR, "pgmemcache: key too long");

  if (PG_NARGS() >= 2)
    offset = PG_GETARG_INT64(1);

  if (offset < 0)
    {
      /* memcached uses uint64_t but postgresql only has signed types, but
       * since we have both increment and decrement operations let's just
       * invert the operation if offset is negative.
       */
      offset = abs(offset);
      increment = !increment;
    }

  if (increment)
    rc = memcached_increment_with_initial(globals.mc, key, key_length, offset, 0, MEMCACHED_EXPIRATION_NOT_ADD, &val);
  else
    rc = memcached_decrement_with_initial(globals.mc, key, key_length, offset, 0, MEMCACHED_EXPIRATION_NOT_ADD, &val);

  if (rc == MEMCACHED_BUFFERED)
    {
      globals.flush_needed = true;
      PG_RETURN_NULL();
    }
  if (rc != MEMCACHED_SUCCESS)
    {
      elog(WARNING, "pgmemcache: memcached_%s_with_initial: %s",
                    increment ? "increment" : "decrement",
                    memcached_strerror(globals.mc, rc));
    }
  else if (val > 0x7FFFFFFFFFFFFFFFLL && val != UINT64_MAX)
    {
      /* Cannot represent uint64_t values above 2^63-1 with BIGINT.  Do
         not signal error for UINT64_MAX which just means there was no
         reply.  */
      elog(ERROR, "pgmemcache: memcached_%s_with_initial: %s",
                  increment ? "increment" : "decrement",
                  "value received from memcache is out of BIGINT range");
    }
  PG_RETURN_INT64(val);
}

Datum memcache_decr(PG_FUNCTION_ARGS)
{
  return memcache_delta_op(false, fcinfo);
}

Datum memcache_incr(PG_FUNCTION_ARGS)
{
  return memcache_delta_op(true, fcinfo);
}

Datum memcache_delete(PG_FUNCTION_ARGS)
{
  text *key_to_be_deleted = PG_GETARG_TEXT_P(0);
  size_t key_length;
  time_t hold;
  memcached_return rc;
  char *key;

  key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(key_to_be_deleted)));
  key_length = strlen(key);
  if (key_length < 1)
    elog(ERROR, "pgmemcache: key cannot be an empty string");
  if (key_length >= 250)
    elog(ERROR, "pgmemcache: key too long");

  hold = (time_t) 0.0;
  if (PG_NARGS() >= 2 && PG_ARGISNULL(1) == false)
    hold = interval_to_time_t(PG_GETARG_INTERVAL_P(1));

  rc = memcached_delete(globals.mc, key, key_length, hold);
  if (rc == MEMCACHED_BUFFERED)
    {
      globals.flush_needed = true;
      PG_RETURN_NULL();
    }
  if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND)
    elog(WARNING, "pgmemcache: memcached_delete: %s",
                  memcached_strerror(globals.mc, rc));

  PG_RETURN_BOOL(rc == MEMCACHED_SUCCESS);
}

Datum memcache_flush_all0(PG_FUNCTION_ARGS)
{
  static time_t opt_expire = 0;
  memcached_return rc;

  rc = memcached_flush(globals.mc, opt_expire);
  if (rc == MEMCACHED_BUFFERED)
    {
      globals.flush_needed = true;
      PG_RETURN_NULL();
    }
  if (rc != MEMCACHED_SUCCESS)
    elog(WARNING, "pgmemcache: memcached_flush: %s",
                  memcached_strerror(globals.mc, rc));

  PG_RETURN_BOOL(rc == MEMCACHED_SUCCESS);
}

Datum memcache_get(PG_FUNCTION_ARGS)
{
  text *get_key, *ret;
  char *key;
#ifdef USE_LIBMEMCACHED
  char *string;
  uint32_t flags;
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
  const unsigned char *string;
#endif /* USE_OMCACHE */
  size_t key_length, return_value_length;
  memcached_return rc;

  if (PG_ARGISNULL(0))
    elog(ERROR, "pgmemcache: key cannot be NULL");

  get_key = PG_GETARG_TEXT_P(0);

  key = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(get_key)));
  key_length = strlen(key);

  if (key_length < 1)
    elog(ERROR, "pgmemcache: key cannot be an empty string");
  if (key_length >= 250)
    elog(ERROR, "pgmemcache: key too long");

#ifdef USE_LIBMEMCACHED
  string = memcached_get(globals.mc, key, key_length, &return_value_length, &flags, &rc);
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
  rc = omcache_get(globals.mc, omc_cc_to_cuc(key), key_length, &string, &return_value_length,
                   NULL, NULL, OMCACHE_READ_TIMEOUT);
#endif /* USE_OMCACHE */

  if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_NOTFOUND)
    elog(ERROR, "pgmemcache: memcached_get: %s",
                memcached_strerror(globals.mc, rc));

  if (rc == MEMCACHED_NOTFOUND)
    PG_RETURN_NULL();

  ret = (text *) palloc(return_value_length + VARHDRSZ);
  SET_VARSIZE(ret, return_value_length + VARHDRSZ);
  memcpy(VARDATA(ret), string, return_value_length);
#ifdef USE_LIBMEMCACHED
  free(string);
#endif /* USE_LIBMEMCACHED */

  PG_RETURN_TEXT_P(ret);
}

Datum memcache_get_multi(PG_FUNCTION_ARGS)
{
  ArrayType *array;
  int array_length, array_lbound, i;
  Oid element_type;
#ifdef USE_LIBMEMCACHED
  uint32_t flags;
#endif /* USE_LIBMEMCACHED */
  memcached_return rc;
  char typalign;
  int16 typlen;
  bool typbyval;
#ifdef USE_OMCACHE
  const unsigned
#endif /* USE_OMCACHE */
  char *current_key, *current_val;
  size_t current_key_len, current_val_len;
  char **keys;
  size_t *key_lens;
  FuncCallContext *funcctx;
  MemoryContext oldcontext;
  TupleDesc tupdesc;
  AttInMetadata *attinmeta;
  struct internal_fctx {
      char **keys;
      size_t *key_lens;
#ifdef USE_OMCACHE
      omcache_req_t *requests;
      size_t request_count;
      omcache_value_t *values;
      size_t value_count;
#endif /* USE_OMCACHE */
  } *fctx;

  if (PG_ARGISNULL(0))
    elog(ERROR, "pgmemcache: get_multi key cannot be null");

  array = PG_GETARG_ARRAYTYPE_P(0);
  if (ARR_NDIM(array) != 1)
    elog(ERROR, "pgmemcache: only single dimension ARRAYs are supported, "
                "not ARRAYs with %d dimensions", ARR_NDIM(array));

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
      fctx = (struct internal_fctx *) palloc(sizeof(*fctx));

      get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

      keys = palloc(sizeof(char *) * (array_length + 1 /* extra key for last memcached_fetch call */ ));
      key_lens = palloc(sizeof(size_t) * (array_length + 1));

      /* initialize terminating extra-key */
      keys[array_length] = 0;
      key_lens[array_length] = 0;

#ifdef USE_OMCACHE
      /* persistent request structures to handle pending requests */
      fctx->requests = palloc(sizeof(omcache_req_t) * array_length);
      fctx->request_count = array_length;
      fctx->values = palloc(sizeof(omcache_value_t) * array_length);
      fctx->value_count = array_length;
#endif /* USE_OMCACHE */

      for (i = 0; i < array_length; i++)
        {
          int offset = array_lbound + i;
          bool isnull;
          Datum elem;

          elem = array_ref(array, 1, &offset, 0, typlen, typbyval, typalign, &isnull);
          if (!isnull)
            {
              keys[i] = TextDatumGetCString(PointerGetDatum(elem));
              key_lens[i] = strlen(keys[i]);
            }
        }
      fctx->keys = keys;
      fctx->key_lens = key_lens;

#ifdef USE_LIBMEMCACHED
      rc = memcached_mget(globals.mc, (const char **) keys, key_lens, array_length);
      if (rc != MEMCACHED_SUCCESS)
        elog(ERROR, "pgmemcache: memcached_mget: %s",
                    memcached_strerror(globals.mc, rc));
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
      rc = omcache_get_multi(globals.mc, (const unsigned char **) keys, key_lens, array_length,
                             fctx->requests, &fctx->request_count, fctx->values, &fctx->value_count,
                             OMCACHE_READ_TIMEOUT);
      if (rc != OMCACHE_OK && rc != OMCACHE_AGAIN)
        elog(ERROR, "pgmemcache: omcache_get_multi: %s", omcache_strerror(rc));
#endif /* USE_OMCACHE */

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

#ifdef USE_LIBMEMCACHED
  current_key = fctx->keys[funcctx->call_cntr];
  current_key_len = fctx->key_lens[funcctx->call_cntr];
  current_val = memcached_fetch(globals.mc, current_key, &current_key_len, &current_val_len, &flags, &rc);
  if (rc == MEMCACHED_END)
    {
      SRF_RETURN_DONE(funcctx);
    }
  else if (rc != MEMCACHED_SUCCESS)
    {
      elog(ERROR, "pgmemcache: memcached_fetch: %s",
                  memcached_strerror(globals.mc, rc));
      SRF_RETURN_DONE(funcctx);
    }
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
  current_val = NULL;
  if (fctx->value_count == 0 && fctx->request_count > 0)
    {
      fctx->value_count = fctx->request_count;
      rc = omcache_io(globals.mc, fctx->requests, &fctx->request_count,
                      fctx->values, &fctx->value_count, OMCACHE_READ_TIMEOUT);
      if (rc != OMCACHE_OK && rc != OMCACHE_AGAIN)
        elog(ERROR, "pgmemcache: omcache_io: %s", omcache_strerror(rc));
    }
  if (fctx->value_count > 0)
    {
      fctx->value_count --;
      current_key = fctx->values[fctx->value_count].key;
      current_key_len = fctx->values[fctx->value_count].key_len;
      current_val = fctx->values[fctx->value_count].data;
      current_val_len = fctx->values[fctx->value_count].data_len;
    }
#endif /* USE_OMCACHE */
  if (current_val != NULL)
    {
      char **values;
      HeapTuple tuple;
      Datum result;

      values = (char **) palloc(2 * sizeof(char *));
      /* make sure we have space for terminating zero character */
      values[0] = (char *) palloc(current_key_len + 1);
      values[1] = (char *) palloc(current_val_len + 1);

      memcpy(values[0], current_key, current_key_len);
      memcpy(values[1], current_val, current_val_len);
#ifdef USE_LIBMEMCACHED
      free(current_val);
#endif /* USE_LIBMEMCACHED */

      /* BuildTupleFromCStrings needs correct zero-terminated C-string, so terminate our raw strings */
      values[0][current_key_len] = '\0';
      values[1][current_val_len] = '\0';

      tuple = BuildTupleFromCStrings(attinmeta, values);
      result = HeapTupleGetDatum(tuple);

      SRF_RETURN_NEXT(funcctx, result);
    }
  SRF_RETURN_DONE(funcctx);
}

Datum memcache_replace(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_REPLACE | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum memcache_replace_absexpire(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_REPLACE | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

Datum memcache_set(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_SET | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum memcache_set_absexpire(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_SET | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

Datum memcache_prepend(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_PREPEND | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum memcache_prepend_absexpire(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_PREPEND | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

Datum memcache_append(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_APPEND | PG_MEMCACHE_TYPE_INTERVAL, fcinfo);
}

Datum memcache_append_absexpire(PG_FUNCTION_ARGS)
{
  return memcache_set_cmd(PG_MEMCACHE_APPEND | PG_MEMCACHE_TYPE_TIMESTAMP, fcinfo);
}

static Datum memcache_set_cmd(int type, PG_FUNCTION_ARGS)
{
  memcached_return rc = MEMCACHED_FAILURE;
  const char *func = NULL;
  char *key, *value;
  text *key_text = NULL, *value_text;
  size_t key_length, value_length;
  time_t expiration = 0;

  if (PG_ARGISNULL(0))
    elog(ERROR, "pgmemcache: key cannot be NULL");
  if (PG_ARGISNULL(1))
    elog(ERROR, "pgmemcache: value cannot be NULL");

  key_text = PG_GETARG_TEXT_P(0);
  key_length = VARSIZE(key_text) - VARHDRSZ;

  /* These aren't really needed as we set libmemcached behavior to check for all invalid sets */
  if (key_length < 1)
    elog(ERROR, "pgmemcache: key cannot be an empty string");
  if (key_length >= 250)
    elog(ERROR, "pgmemcache: key too long");

  value_text = PG_GETARG_TEXT_P(1);
  value_length = VARSIZE(value_text) - VARHDRSZ;

  if (PG_NARGS() >= 3 && PG_ARGISNULL(2) == false)
    {
      if (type & PG_MEMCACHE_TYPE_INTERVAL)
        {
          Interval *span = PG_GETARG_INTERVAL_P(2);
          expiration = interval_to_time_t(span);
        }
      else if (type & PG_MEMCACHE_TYPE_TIMESTAMP)
        {
          TimestampTz timestamptz;
          struct pg_tm tm;
          fsec_t fsec;

          timestamptz = PG_GETARG_TIMESTAMPTZ(2);

          /* convert to timestamptz to produce consistent results */
          if (timestamp2tm(timestamptz, NULL, &tm, &fsec, NULL, NULL) !=0)
            ereport(ERROR,
                    (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
                     errmsg("timestamp out of range")));

#ifdef HAVE_INT64_TIMESTAMP
          expiration = (time_t) ((timestamptz - SetEpochTimestamp()) / 1000000e0);
#else
          expiration = (time_t) timestamptz - SetEpochTimestamp();
#endif
        }
      else
        {
          elog(ERROR, "%s():%s:%u: invalid date type", __FUNCTION__, __FILE__, __LINE__);
        }
    }

  key = VARDATA(key_text);
  value = VARDATA(value_text);

  if (type & PG_MEMCACHE_ADD)
    {
      func = "memcached_add";
      rc = memcached_add(globals.mc, key, key_length, value, value_length, expiration, 0);
    }
  else if (type & PG_MEMCACHE_REPLACE)
    {
      func = "memcached_replace";
      rc = memcached_replace(globals.mc, key, key_length, value, value_length, expiration, 0);
    }
  else if (type & PG_MEMCACHE_SET)
    {
      func = "memcached_set";
      rc = memcached_set(globals.mc, key, key_length, value, value_length, expiration, 0);
    }
  else if (type & PG_MEMCACHE_PREPEND)
    {
      func = "memcached_prepend";
      rc = memcached_prepend(globals.mc, key, key_length, value, value_length, expiration, 0);
    }
  else if (type & PG_MEMCACHE_APPEND)
    {
      func = "memcached_append";
      rc = memcached_append(globals.mc, key, key_length, value, value_length, expiration, 0);
    }
  else
    {
      elog(ERROR, "pgmemcache: unknown set command type: %d", type);
      return false;
    }

  if (rc == MEMCACHED_BUFFERED)
    {
      globals.flush_needed = true;
      PG_RETURN_NULL();
    }
  if (rc != MEMCACHED_SUCCESS)
    elog(WARNING, "pgmemcache: %s: %s", func,
                  memcached_strerror(globals.mc, rc));

  PG_RETURN_BOOL(rc == MEMCACHED_SUCCESS);
}

Datum memcache_server_add(PG_FUNCTION_ARGS)
{
  text *server = PG_GETARG_TEXT_P(0);
  char *host_str;
  memcached_return rc;

  host_str = DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(server)));

  rc = do_server_add(host_str);
  if (rc != MEMCACHED_SUCCESS)
    elog(WARNING, "pgmemcache: memcached_server_push: %s",
                  memcached_strerror(globals.mc, rc));

  PG_RETURN_BOOL(rc == MEMCACHED_SUCCESS);
}

static memcached_return do_server_add(const char *host_str)
{
  memcached_server_st *servers;
  memcached_return rc;

  servers = memcached_servers_parse(host_str);
  rc = memcached_server_push(globals.mc, servers);
  memcached_server_list_free(servers);

  return rc;
}

#ifdef USE_LIBMEMCACHED
#define MC_ENUM_INVAL -1
#define MC_STR_TO_ENUM(d,v) \
  if (strcmp(value, "MEMCACHED_" #d "_" #v) == 0 || strcmp(value, #v) == 0) \
      return MEMCACHED_##d##_##v
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
#define MC_ENUM_INVAL NULL
#define MC_STR_TO_ENUM(d,v) \
  if (strcmp(value, "MEMCACHED_" #d "_" #v) == 0 || strcmp(value, #v) == 0) \
      return #v
#endif /* USE_OMCACHE */

static memcached_behavior get_memcached_behavior_flag(const char *value)
{
  MC_STR_TO_ENUM(BEHAVIOR, BINARY_PROTOCOL);
  MC_STR_TO_ENUM(BEHAVIOR, BUFFER_REQUESTS);
  MC_STR_TO_ENUM(BEHAVIOR, CACHE_LOOKUPS);
  MC_STR_TO_ENUM(BEHAVIOR, CONNECT_TIMEOUT);
#if LIBMEMCACHED_VERSION_HEX >= 0x01000003
  MC_STR_TO_ENUM(BEHAVIOR, DEAD_TIMEOUT);
#endif
  MC_STR_TO_ENUM(BEHAVIOR, DISTRIBUTION);
  MC_STR_TO_ENUM(BEHAVIOR, HASH);
  MC_STR_TO_ENUM(BEHAVIOR, HASH_WITH_PREFIX_KEY);
  MC_STR_TO_ENUM(BEHAVIOR, IO_BYTES_WATERMARK);
  MC_STR_TO_ENUM(BEHAVIOR, IO_KEY_PREFETCH);
  MC_STR_TO_ENUM(BEHAVIOR, IO_MSG_WATERMARK);
  MC_STR_TO_ENUM(BEHAVIOR, KETAMA);
  MC_STR_TO_ENUM(BEHAVIOR, KETAMA_HASH);
#ifdef USE_OMCACHE
  MC_STR_TO_ENUM(BEHAVIOR, KETAMA_PRE1010);
#endif
  MC_STR_TO_ENUM(BEHAVIOR, KETAMA_WEIGHTED);
  MC_STR_TO_ENUM(BEHAVIOR, NO_BLOCK);
  MC_STR_TO_ENUM(BEHAVIOR, NOREPLY);
  MC_STR_TO_ENUM(BEHAVIOR, NUMBER_OF_REPLICAS);
  MC_STR_TO_ENUM(BEHAVIOR, POLL_TIMEOUT);
  MC_STR_TO_ENUM(BEHAVIOR, RANDOMIZE_REPLICA_READ);
  MC_STR_TO_ENUM(BEHAVIOR, RCV_TIMEOUT);
#if LIBMEMCACHED_VERSION_HEX >= 0x00049000
  MC_STR_TO_ENUM(BEHAVIOR, REMOVE_FAILED_SERVERS);
#endif
  MC_STR_TO_ENUM(BEHAVIOR, RETRY_TIMEOUT);
  MC_STR_TO_ENUM(BEHAVIOR, SERVER_FAILURE_LIMIT);
  MC_STR_TO_ENUM(BEHAVIOR, SND_TIMEOUT);
  MC_STR_TO_ENUM(BEHAVIOR, SOCKET_RECV_SIZE);
  MC_STR_TO_ENUM(BEHAVIOR, SOCKET_SEND_SIZE);
  MC_STR_TO_ENUM(BEHAVIOR, SORT_HOSTS);
  MC_STR_TO_ENUM(BEHAVIOR, SUPPORT_CAS);
  MC_STR_TO_ENUM(BEHAVIOR, TCP_NODELAY);
  MC_STR_TO_ENUM(BEHAVIOR, USER_DATA);
  MC_STR_TO_ENUM(BEHAVIOR, USE_UDP);
  MC_STR_TO_ENUM(BEHAVIOR, VERIFY_KEY);

  elog(ERROR, "pgmemcache: unknown behavior flag: %s", value);
  return MC_ENUM_INVAL;
}

static memcached_hash get_memcached_hash_type(const char *value)
{
  MC_STR_TO_ENUM(HASH, MURMUR);
  MC_STR_TO_ENUM(HASH, MD5);
  MC_STR_TO_ENUM(HASH, JENKINS);
  MC_STR_TO_ENUM(HASH, HSIEH);
  MC_STR_TO_ENUM(HASH, FNV1A_64);
  MC_STR_TO_ENUM(HASH, FNV1A_32);
  MC_STR_TO_ENUM(HASH, FNV1_64);
  MC_STR_TO_ENUM(HASH, FNV1_32);
  MC_STR_TO_ENUM(HASH, DEFAULT);
  MC_STR_TO_ENUM(HASH, CRC);

  elog(ERROR, "pgmemcache: invalid hash name: %s", value);
  return MC_ENUM_INVAL;
}

static memcached_server_distribution get_memcached_distribution_type(const char *value)
{
  MC_STR_TO_ENUM(DISTRIBUTION, RANDOM);
  MC_STR_TO_ENUM(DISTRIBUTION, MODULA);
  MC_STR_TO_ENUM(DISTRIBUTION, CONSISTENT_KETAMA);
  MC_STR_TO_ENUM(DISTRIBUTION, CONSISTENT);

  elog(ERROR, "pgmemcache: invalid distribution name: %s", value);
  return MC_ENUM_INVAL;
}

static uint64_t get_memcached_behavior_data(const char *flag, const char *data, const char **val)
{
  char *endptr;
  uint64_t ret;
  memcached_behavior bkey = get_memcached_behavior_flag(flag);

#ifdef USE_LIBMEMCACHED
  switch (bkey)
    {
    case MEMCACHED_BEHAVIOR_HASH:
    case MEMCACHED_BEHAVIOR_KETAMA_HASH:
      return get_memcached_hash_type(data);
    case MEMCACHED_BEHAVIOR_DISTRIBUTION:
      return get_memcached_distribution_type(data);
    default:
#endif /* USE_LIBMEMCACHED */
#ifdef USE_OMCACHE
  ret = 0;
  if (strcmp(bkey, "HASH") == 0 || strcmp(bkey, "KETAMA_HASH") == 0)
    *val = get_memcached_hash_type(data);
  else if (strcmp(bkey, "DISTRIBUTION") == 0)
    *val = get_memcached_distribution_type(data);
  else
    {
#endif /* USE_OMCACHE */
      ret = strtol(data, &endptr, 10);
      if (endptr == data)
        elog(ERROR, "pgmemcache: invalid behavior param %s: %s", flag, data);
    }
  return ret;
}

/* NOTE: memcached_server_fn specifies that the first argument is const, but
 * memcached_stat_get_keys wants a non-const argument so we don't define it
 * as const here.
 */
static memcached_return_t server_stat_function(memcached_st *mc,
                                               memcached_server_instance_st server,
                                               void *context)
{
  memcached_return rc;
  StringInfoData *strbuf = (StringInfoData *) context;
  const char *hostname = memcached_server_name(server);
  unsigned int port = memcached_server_port(server);

  appendStringInfo(strbuf, "Server: %s (%u)\n", hostname, port);

  {
#ifdef USE_LIBMEMCACHED
  memcached_stat_st stat;
  char **list, **stat_ptr;

  rc = memcached_stat_servername(&stat, NULL, hostname, port);
  if (rc != MEMCACHED_SUCCESS)
    return rc;

  list = memcached_stat_get_keys(mc, &stat, &rc);
  if (rc != MEMCACHED_SUCCESS)
    return rc;

  for (stat_ptr = list; stat_ptr && *stat_ptr; stat_ptr++)
    {
      char *value = memcached_stat_get_value(mc, &stat, *stat_ptr, &rc);
      appendStringInfo(strbuf, "%s: %s\n", *stat_ptr, value);
      free(value);
    }
  free(list);
#endif /* USE_LIBMEMCACHED */

#ifdef USE_OMCACHE
  size_t i, value_count = 50;
  omcache_value_t values[50];
  rc = omcache_stat(globals.mc, NULL, values, &value_count,
                    server->server_index, OMCACHE_READ_TIMEOUT);
  if (rc != OMCACHE_OK)
    {
      value_count = 0;
      appendStringInfo(strbuf, "omcache_stat failed: %s\n", omcache_strerror(rc));
    }

  for (i = 0; i < value_count; i++)
    {
      int key_len = (int) values[i].key_len,
          data_len = (int) values[i].data_len;
      if (key_len == 0 && data_len == 0)
        break;
      appendStringInfo(strbuf, "%.*s: %.*s\n",
                       key_len, (const char *) values[i].key,
                       data_len, (const char *) values[i].data);
    }
#endif /* USE_OMCACHE */
  }

  appendStringInfo(strbuf, "\n");
  return MEMCACHED_SUCCESS;
}

Datum memcache_stats(PG_FUNCTION_ARGS)
{
  StringInfoData strbuf;
  memcached_return rc;
  memcached_server_fn callbacks[1];

  initStringInfo(&strbuf);
  callbacks[0] = (memcached_server_fn) server_stat_function;
  rc = memcached_server_cursor(globals.mc, callbacks, (void *) &strbuf, 1);

  if (rc != MEMCACHED_SUCCESS && rc != MEMCACHED_SOME_ERRORS)
    elog(WARNING, "pgmemcache: memcache_stats: %s",
                  memcached_strerror(globals.mc, rc));

  PG_RETURN_DATUM(DirectFunctionCall1(textin, CStringGetDatum(strbuf.data)));
}
