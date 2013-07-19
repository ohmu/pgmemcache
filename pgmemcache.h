/*
 * PostgreSQL functions to interface with memcache.
 *
 * Copyright (c) 2004-2005 Sean Chittenden <sean@chittenden.org>
 * Copyright (c) 2007 Neil Conway <neilc@samurai.com>
 * Copyright (c) 2007 Open Technology Group, Inc. <http://www.otg-nc.com>
 * Copyright (c) 2008-2011 Hannu Valtonen <hannu.valtonen@ormod.com>
 *
 * See the file COPYING for distribution terms.
 */
#ifndef PGMEMCACHE_H
#define PGMEMCACHE_H

#include <libmemcached/memcached.h>
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

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

void _PG_init(void);
void _PG_fini(void);

/* Custom GUC variable */
static void assign_default_servers_guc(const char *newval, void *extra);
static void assign_default_behavior_guc(const char *newval, void *extra);
static memcached_behavior get_memcached_behavior_flag(const char *flag);
static uint64_t get_memcached_behavior_data(const char *flag, const char *data);
static uint64_t get_memcached_hash_type(const char *data);
static uint64_t get_memcached_distribution_type(const char *data);
static Datum memcache_atomic_op(bool increment, PG_FUNCTION_ARGS);
static Datum memcache_set_cmd(int type, PG_FUNCTION_ARGS);
static memcached_return do_server_add(const char *host_str);
static bool do_memcache_set_cmd(int type, char *key, size_t key_len, char *val, size_t val_len, time_t expire);
static time_t interval_to_time_t(Interval *span);
static void *pgmemcache_malloc(memcached_st *ptr __attribute__((unused)), const size_t, void *context);
static void pgmemcache_free(memcached_st *ptr __attribute__((unused)), void *mem, void *context);
static void *pgmemcache_realloc(memcached_st *ptr __attribute__((unused)), void *, const size_t, void *context);
static void *pgmemcache_calloc(memcached_st *ptr __attribute__((unused)), size_t nelem, const size_t, void *context);

#define PG_MEMCACHE_ADD                 0x0001
#define PG_MEMCACHE_REPLACE             0x0002
#define PG_MEMCACHE_SET                 0x0004
#define PG_MEMCACHE_PREPEND             0x0008
#define PG_MEMCACHE_APPEND              0x0010
#define PG_MEMCACHE_TYPE_INTERVAL       0x0100
#define PG_MEMCACHE_TYPE_TIMESTAMP      0x0200

Datum memcache_add(PG_FUNCTION_ARGS);
Datum memcache_add_absexpire(PG_FUNCTION_ARGS);
Datum memcache_decr(PG_FUNCTION_ARGS);
Datum memcache_delete(PG_FUNCTION_ARGS);
Datum memcache_flush_all0(PG_FUNCTION_ARGS);
Datum memcache_get(PG_FUNCTION_ARGS);
Datum memcache_get_multi(PG_FUNCTION_ARGS);
Datum memcache_incr(PG_FUNCTION_ARGS);
Datum memcache_replace(PG_FUNCTION_ARGS);
Datum memcache_replace_absexpire(PG_FUNCTION_ARGS);
Datum memcache_server_add(PG_FUNCTION_ARGS);
Datum memcache_set(PG_FUNCTION_ARGS);
Datum memcache_set_absexpire(PG_FUNCTION_ARGS);
Datum memcache_prepend(PG_FUNCTION_ARGS);
Datum memcache_prepend_absexpire(PG_FUNCTION_ARGS);
Datum memcache_append(PG_FUNCTION_ARGS);
Datum memcache_append_absexpire(PG_FUNCTION_ARGS);
Datum memcache_stats(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(memcache_add);
PG_FUNCTION_INFO_V1(memcache_add_absexpire);
PG_FUNCTION_INFO_V1(memcache_decr);
PG_FUNCTION_INFO_V1(memcache_delete);
PG_FUNCTION_INFO_V1(memcache_flush_all0);
PG_FUNCTION_INFO_V1(memcache_get);
PG_FUNCTION_INFO_V1(memcache_get_multi);
PG_FUNCTION_INFO_V1(memcache_incr);
PG_FUNCTION_INFO_V1(memcache_replace);
PG_FUNCTION_INFO_V1(memcache_replace_absexpire);
PG_FUNCTION_INFO_V1(memcache_server_add);
PG_FUNCTION_INFO_V1(memcache_set);
PG_FUNCTION_INFO_V1(memcache_set_absexpire);
PG_FUNCTION_INFO_V1(memcache_prepend);
PG_FUNCTION_INFO_V1(memcache_prepend_absexpire);
PG_FUNCTION_INFO_V1(memcache_append);
PG_FUNCTION_INFO_V1(memcache_append_absexpire);
PG_FUNCTION_INFO_V1(memcache_stats);

#endif /* !PGMEMCACHE_H */
