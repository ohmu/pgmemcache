/*
 * PostgreSQL functions to interface with memcache.
 *
 * $PostgreSQL$
 *
 * Copyright (c) 2004 Sean Chittenden <sean@chittenden.org>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * http://people.FreeBSD.org/~seanc/pgmemcache/
 */

#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "utils/datetime.h"
#include "utils/palloc.h"

#include "pgmemcache.h"

/* Global memcache instance. */
static struct memcache *mc = NULL;
static struct memcache_ctxt *ctxt = NULL;

/* Add a small work around function to compensate for PostgreSQLs lack
 * of a pstrdup(3) function that allocates memory from the upper
 * layer's SPI context. */
inline static void	 mcm_pfree(void *ptr);
inline static void	*mcm_palloc(const size_t size);
inline static void	*mcm_repalloc(void *ptr, const size_t size);
inline static char	*mcm_pstrdup(const char *str);
static bool		 _memcache_init(void);
static Datum		 memcache_atomic_op(int type, PG_FUNCTION_ARGS);
static Datum		 memcache_set_cmd(int type, PG_FUNCTION_ARGS);

#define MCM_SET_CMD_TYPE_ADD 0x0001
#define MCM_SET_CMD_TYPE_REPLACE 0x0002
#define MCM_SET_CMD_TYPE_SET 0x0004

#define MCM_DATE_TYPE_INTERVAL 0x0010
#define MCM_DATE_TYPE_TIMESTAMP 0x0020

#define MCM_ATOMIC_TYPE_DECR 0x0040
#define MCM_ATOMIC_TYPE_INCR 0x0080

/* Don't add a server until a caller has called memcache_init().  This
 * is probably an unnecessary degree of explicitness, but it forces
 * good calling conventions for pgmemcache.  */
#define MCM_CHECK(_ret) do { \
  if (mc == NULL || ctxt == NULL) { \
    elog(ERROR, "%s(): mc is NULL, call memcache_init()", __FUNCTION__); \
    _ret; \
  } \
} while(0)

/* Add a small work around function to compensate for PostgreSQLs lack
 * of a pstrdup(3), palloc(3), pfree(3), and prealloc(3) functions.
 * They exist, but are macros, which is useless with regards to
 * function pointers if the macro doesn't have the same signature.
 * Create that signature here. */
inline static void
mcm_pfree(void *ptr) {
  return pfree(ptr);
}


inline static void *
mcm_palloc(const size_t size) {
  return palloc(size);
}


inline static void *
mcm_repalloc(void *ptr, const size_t size) {
  return repalloc(ptr, size);
}


inline static char *
mcm_pstrdup(const char *str) {
  return pstrdup(str);
}


Datum
memcache_add(PG_FUNCTION_ARGS) {
  return memcache_set_cmd(MCM_SET_CMD_TYPE_ADD | MCM_DATE_TYPE_INTERVAL, fcinfo);
}


Datum
memcache_add_absexpire(PG_FUNCTION_ARGS) {
  return memcache_set_cmd(MCM_SET_CMD_TYPE_ADD | MCM_DATE_TYPE_TIMESTAMP, fcinfo);
}


static Datum
memcache_atomic_op(int type, PG_FUNCTION_ARGS) {
  text		*key;
  size_t	 key_len;
  u_int32_t	 val,
		 ret = 0;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  val = 1;
  if (fcinfo->arg[1] != NULL && !PG_ARGISNULL(1)) {
    val = PG_GETARG_UINT32(1);
  }

  if (type & MCM_ATOMIC_TYPE_DECR)
    ret = mcm_decr(ctxt, mc, VARDATA(key), key_len, val);
  else if (type & MCM_ATOMIC_TYPE_INCR)
    ret = mcm_incr(ctxt, mc, VARDATA(key), key_len, val);
  else
    elog(ERROR, "%s():%s:%u\tunknown atomic type 0x%x", __FUNCTION__, __FILE__, __LINE__, type);

  SPI_finish();

  PG_RETURN_UINT32(ret);
}


Datum
memcache_decr(PG_FUNCTION_ARGS) {
  return memcache_atomic_op(MCM_ATOMIC_TYPE_DECR, fcinfo);
}


Datum
memcache_delete(PG_FUNCTION_ARGS) {
  text		*key;
  size_t	 key_len;
  Interval	*span;
  float8	 hold;
  int		 ret;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  hold = 0.0;
  if (fcinfo->arg[1] != NULL && !PG_ARGISNULL(1)) {
    span = PG_GETARG_INTERVAL_P(1);

#ifdef HAVE_INT64_TIMESTAMP
    hold = (span->time / 1000000e0);
#else
    hold = span->time;
#endif
    if (span->month != 0) {
      hold += ((365.25 * 86400) * (span->month / 12));
      hold += ((30.0 * 86400) * (span->month % 12));
    }
  }

  ret = mcm_delete(ctxt, mc, VARDATA(key), key_len, (time_t)hold);

  SPI_finish();

  if (ret == 0)
    PG_RETURN_BOOL(true);
  else if (ret > 0)
    PG_RETURN_BOOL(false);
  else
    elog(ERROR, "Internal libmemcache(3) error");

  PG_RETURN_BOOL(ret);
}


/* Depreciated: use memcache_flush() */
Datum
memcache_flush_all(PG_FUNCTION_ARGS) {
  return memcache_flush(fcinfo);
}


Datum
memcache_flush_all0(PG_FUNCTION_ARGS) {
  text		*key;
  size_t	 key_len;
  int		 ret;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  ret = mcm_flush_all(ctxt, mc);

  SPI_finish();

  if (ret == 0)
    PG_RETURN_BOOL(true);
  else if (ret > 0)
    PG_RETURN_BOOL(false);
  else
    elog(ERROR, "Internal libmemcache(3) error");

  PG_RETURN_BOOL(ret);
}


Datum
memcache_flush(PG_FUNCTION_ARGS) {
  struct memcache_server *ms;
  text		*key;
  size_t	 key_len;
  int		 ret;
  u_int32_t	 hash;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  hash = mcm_hash_key(ctxt, VARDATA(key), key_len);
  ms = mcm_find_server(ctxt, mc, hash);

  ret = mcm_flush(ctxt, mc, ms);

  SPI_finish();

  if (ret == 0)
    PG_RETURN_BOOL(true);
  else if (ret > 0)
    PG_RETURN_BOOL(false);
  else
    elog(ERROR, "Internal libmemcache(3) error");

  PG_RETURN_BOOL(ret);
}


Datum
memcache_free(PG_FUNCTION_ARGS) {
  MCM_CHECK(PG_RETURN_BOOL(false));

  SPI_connect();

  mcm_free(ctxt, mc);
  mc = NULL;

  mcMemFreeCtxt(ctxt);
  ctxt = NULL;

  SPI_finish();

  PG_RETURN_BOOL(true);
}


Datum
memcache_get(PG_FUNCTION_ARGS) {
  text			*key,
			*ret;
  size_t		 key_len;
  struct memcache_req	*req;
  struct memcache_res	*res;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  req = mcm_req_new(ctxt);
  if (req == NULL)
    elog(ERROR, "Unable to create a new memcache request structure");

  res = mcm_req_add(ctxt, req, VARDATA(key), key_len);
  if (res == NULL)
    elog(ERROR, "Unable to create a new memcache responose structure");

  mcm_get(ctxt, mc, req);

  if (!(res->_flags & MCM_RES_FOUND)) {
    SPI_finish();
    PG_RETURN_NULL();
  }

  ret = (text *)SPI_palloc(res->bytes + VARHDRSZ);
  VARATT_SIZEP(ret) = res->bytes + VARHDRSZ;
  memcpy(VARDATA(ret), res->val, res->bytes);

  SPI_finish();

  PG_RETURN_TEXT_P(ret);
}


Datum
memcache_hash(PG_FUNCTION_ARGS) {
  text		*key;
  size_t	 key_len;
  u_int32_t	 hash;

  SPI_connect();

  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  hash = mcm_hash_key(ctxt, VARDATA(key), key_len);

  SPI_finish();

  PG_RETURN_UINT32(hash);
}


Datum
memcache_incr(PG_FUNCTION_ARGS) {
  return memcache_atomic_op(MCM_ATOMIC_TYPE_INCR, fcinfo);
}


Datum
memcache_init(PG_FUNCTION_ARGS) {
  bool ret;

  SPI_connect();
  ret = _memcache_init();
  SPI_finish();
  PG_RETURN_BOOL(ret);
}


/* Caller of _memcache_init() must call SPI_connect() before calling
 * _memcache_init(). */
static bool
_memcache_init(void) {
  MemoryContext	oc;

  /* Return FALSE that way callers can conditionally add servers to
   * the instance. */
  if (ctxt != NULL)
    return false;

  /* Make sure mc is allocated in the top memory context */
  oc = MemoryContextSwitchTo(TopMemoryContext);

  /* Initialize libmemcache's memory functions */
  ctxt = mcMemNewCtxt(mcm_pfree, mcm_palloc, NULL, mcm_repalloc, mcm_pstrdup);

  mc = mcm_new(ctxt);
  if (mc == NULL)
    elog(ERROR, "memcache_init: unable to allocate memory");

  /* Return to old memory context */
  MemoryContextSwitchTo(oc);

  return true;
}


Datum
memcache_replace(PG_FUNCTION_ARGS) {
  return memcache_set_cmd(MCM_SET_CMD_TYPE_REPLACE | MCM_DATE_TYPE_INTERVAL, fcinfo);
}


Datum
memcache_replace_absexpire(PG_FUNCTION_ARGS) {
  return memcache_set_cmd(MCM_SET_CMD_TYPE_REPLACE | MCM_DATE_TYPE_TIMESTAMP, fcinfo);
}


Datum
memcache_set(PG_FUNCTION_ARGS) {
  return memcache_set_cmd(MCM_SET_CMD_TYPE_SET | MCM_DATE_TYPE_INTERVAL, fcinfo);
}


Datum
memcache_set_absexpire(PG_FUNCTION_ARGS) {
  return memcache_set_cmd(MCM_SET_CMD_TYPE_SET | MCM_DATE_TYPE_TIMESTAMP, fcinfo);
}


static Datum
memcache_set_cmd(int type, PG_FUNCTION_ARGS) {
  text		*key,
		*val_t;
  char		*val;
  size_t	 key_len, val_len;
  Interval	*span;
  float8	 expire;
  TimestampTz	 timestamptz;
  struct pg_tm	 tm;
  fsec_t	 fsec;
  u_int16_t	 flags;
  int		 ret = 0;

  MCM_CHECK(PG_RETURN_BOOL(false));

  SPI_connect();

  if (PG_ARGISNULL(0)) {
    elog(ERROR, "Unable to have a NULL key");
  }
  key = PG_GETARG_TEXT_P(0);
  key_len = VARSIZE(key) - VARHDRSZ;
  if (key_len < 1)
    elog(ERROR, "Unable to have a zero length key");

  if (fcinfo->arg[1] == NULL || PG_ARGISNULL(1)) {
    val_t = NULL;
    val = NULL;
    val_len = 0;
  } else {
    val_t = PG_GETARG_TEXT_P(1);
    val = (char *)VARDATA(val_t);
    val_len = VARSIZE(val_t) - VARHDRSZ;
  }

  expire = 0.0;
  if (fcinfo->arg[2] != NULL && !PG_ARGISNULL(2)) {
    if (type & MCM_DATE_TYPE_INTERVAL) {
      span = PG_GETARG_INTERVAL_P(2);
#ifdef HAVE_INT64_TIMESTAMP
      expire = (span->time / 1000000e0);
#else
      expire = span->time;
#endif
      if (span->month != 0) {
	expire += ((365.25 * 86400) * (span->month / 12));
	expire += ((30.0 * 86400) * (span->month % 12));
      }
    } else if (type & MCM_DATE_TYPE_TIMESTAMP) {
      timestamptz = PG_GETARG_TIMESTAMPTZ(2);

      /* convert to timestamptz to produce consistent results */
      if (timestamp2tm(timestamptz, NULL, &tm, &fsec, NULL) !=0)
	ereport(ERROR, (errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
			errmsg("timestamp out of range")));

#ifdef HAVE_INT64_TIMESTAMP
      expire = ((timestamptz - SetEpochTimestamp()) / 1000000e0);
#else
      expire = timestamptz - SetEpochTimestamp();
#endif
    } else {
      elog(ERROR, "%s():%s:%u: invalid date type", __FUNCTION__, __FILE__, __LINE__);
    }
  }

  flags = 0;
  if (fcinfo->arg[3] != NULL && !PG_ARGISNULL(3))
    flags = PG_GETARG_INT16(3);

  if (type & MCM_SET_CMD_TYPE_ADD)
    ret = mcm_add(ctxt, mc, VARDATA(key), key_len, val, val_len, (time_t)expire, flags);
  else if (type & MCM_SET_CMD_TYPE_REPLACE)
    ret = mcm_add(ctxt, mc, VARDATA(key), key_len, val, val_len, (time_t)expire, flags);
  else if (type & MCM_SET_CMD_TYPE_SET)
    ret = mcm_add(ctxt, mc, VARDATA(key), key_len, val, val_len, (time_t)expire, flags);
  else
    elog(ERROR, "%s():%s:%u\tunknown set type 0x%x", __FUNCTION__, __FILE__, __LINE__, type);

  SPI_finish();
  if (ret == 0)
    PG_RETURN_BOOL(true);
  else if (ret > 0)
    PG_RETURN_BOOL(false);
  else
    elog(ERROR, "Internal libmemcache(3) error");

  /* Not reached */
  abort();
}


Datum
memcache_server_add(PG_FUNCTION_ARGS) {
  text *server, *port;
  int ret;
  MemoryContext oc;

  MCM_CHECK(PG_RETURN_BOOL(false));

  SPI_connect();

  /* Make sure new servers are in the top memory context */
  oc = MemoryContextSwitchTo(TopMemoryContext);

  server = PG_GETARG_TEXT_P(0);
  port = PG_GETARG_TEXT_P(1);

  elog(DEBUG1, "%s(): host=\"%.*s\" port=\"%.*s\"", __FUNCTION__, VARSIZE(server) - VARHDRSZ, VARDATA(server), VARSIZE(port) - VARHDRSZ, VARDATA(port));

  /* Add the server and port */
  ret = mcm_server_add2(ctxt, mc, VARDATA(server), VARSIZE(server) - VARHDRSZ, VARDATA(port), VARSIZE(port) - VARHDRSZ);
  if (ret < 0) {
    elog(NOTICE, "%s(): libmemcache unable to add server: %d", __FUNCTION__, ret);
    MemoryContextSwitchTo(oc);
    SPI_finish();
    PG_RETURN_BOOL(false);
  }

  /* Return to current context */
  MemoryContextSwitchTo(oc);

  SPI_finish();

  PG_RETURN_BOOL(true);
}


Datum
memcache_stats(PG_FUNCTION_ARGS) {
  struct memcache_server_stats	*stats;
  text		*ret;
  StringInfo	 str;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  stats = mcm_stats(ctxt, mc);
  if (stats == NULL)
    elog(ERROR, "Unable to create a new memcache stats structure");

  str = makeStringInfo();
  if (str == NULL)
    elog(ERROR, "Unable to create a new string object for stats");

  appendStringInfo(str, "pid: %u\n", stats->pid);
  appendStringInfo(str, "uptime: %llu\n", (uint64)stats->uptime);
  appendStringInfo(str, "time: %llu\n", (uint64)stats->time);
  appendStringInfo(str, "version: %s\n", stats->version);
  appendStringInfo(str, "rusage_user: %u.%us\n", stats->rusage_user.tv_sec, stats->rusage_user.tv_usec);
  appendStringInfo(str, "rusage_system: %u.%us\n", stats->rusage_system.tv_sec, stats->rusage_system.tv_usec);
  appendStringInfo(str, "curr_items: %u\n", stats->curr_items);
  appendStringInfo(str, "total_items: %llu\n", stats->total_items);
  appendStringInfo(str, "bytes: %llu\n", stats->bytes);
  appendStringInfo(str, "curr_connections: %u\n", stats->curr_connections);
  appendStringInfo(str, "total_connections: %llu\n", stats->total_connections);
  appendStringInfo(str, "connection_structures: %u\n", stats->connection_structures);
  appendStringInfo(str, "cmd_get: %llu\n", stats->cmd_get);
  appendStringInfo(str, "cmd_set: %llu\n", stats->cmd_set);
  appendStringInfo(str, "get_hits: %llu\n", stats->get_hits);
  appendStringInfo(str, "get_misses: %llu\n", stats->get_misses);
  appendStringInfo(str, "bytes_read: %llu\n", stats->bytes_read);
  appendStringInfo(str, "bytes_written: %llu\n", stats->bytes_written);
  appendStringInfo(str, "limit_maxbytes: %llu\n", stats->limit_maxbytes);

  ret = (text *)SPI_palloc(str->len + VARHDRSZ);
  VARATT_SIZEP(ret) = str->len + VARHDRSZ;
  memcpy(VARDATA(ret), str->data, str->len);

  SPI_finish();

  PG_RETURN_TEXT_P(ret);
}


Datum
memcache_stat(PG_FUNCTION_ARGS) {
  struct memcache_server_stats	*stats;
  text		*ret, *stat;
  size_t	 stat_len;
  StringInfo	 str;

  MCM_CHECK(PG_RETURN_NULL());

  SPI_connect();

  stat = PG_GETARG_TEXT_P(0);
  stat_len = VARSIZE(stat) - VARHDRSZ;
  if (stat_len < 1)
    elog(ERROR, "Unable to have a zero length stat");

  stats = mcm_stats(ctxt, mc);
  if (stats == NULL)
    elog(ERROR, "Unable to create a new memcache stats structure");

  str = makeStringInfo();
  if (str == NULL)
    elog(ERROR, "Unable to create a new string object for stats");

  if (pg_strncasecmp("pid", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%u", stats->pid);
  else if (pg_strncasecmp("uptime", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", (uint64)stats->uptime);
  else if (pg_strncasecmp("time", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", (uint64)stats->time);
  else if (pg_strncasecmp("version", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%s", stats->version);
  else if (pg_strncasecmp("rusage_user", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%u.%u", stats->rusage_user.tv_sec, stats->rusage_user.tv_usec);
  else if (pg_strncasecmp("rusage_system", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%u.%u", stats->rusage_system.tv_sec, stats->rusage_system.tv_usec);
  else if (pg_strncasecmp("curr_items", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%u", stats->curr_items);
  else if (pg_strncasecmp("total_itmes", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->total_items);
  else if (pg_strncasecmp("bytes", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->bytes);
  else if (pg_strncasecmp("curr_connections", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%u", stats->curr_connections);
  else if (pg_strncasecmp("total_connections", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->total_connections);
  else if (pg_strncasecmp("connection_structures", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%u", stats->connection_structures);
  else if (pg_strncasecmp("cmd_get", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->cmd_get);
  else if (pg_strncasecmp("cmd_set", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->cmd_set);
  else if (pg_strncasecmp("get_hits", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->get_hits);
  else if (pg_strncasecmp("get_misses", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->get_misses);
  else if (pg_strncasecmp("bytes_read", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->bytes_read);
  else if (pg_strncasecmp("bytes_written", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->bytes_written);
  else if (pg_strncasecmp("limit_maxbytes", VARDATA(stat), stat_len) == 0)
    appendStringInfo(str, "%llu", stats->limit_maxbytes);
  else
    elog(ERROR, "Unknown memcache statistic \"%.*s\"", (int)stat_len, VARDATA(stat));

  ret = (text *)SPI_palloc(str->len + VARHDRSZ);
  VARATT_SIZEP(ret) = str->len + VARHDRSZ;
  memcpy(VARDATA(ret), str->data, str->len);

  SPI_finish();

  PG_RETURN_TEXT_P(ret);
}
