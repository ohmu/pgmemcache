/*
 * PostgreSQL functions to interface with memcache.
 *
 * Copyright (c) 2004-2005 Sean Chittenden <sean@chittenden.org>
 * Copyright (c) 2007 Neil Conway <neilc@samurai.com>
 * Copyright (c) 2007 Open Technology Group, Inc. <http://www.otg-nc.com>
 * Copyright (c) 2008-2009 Hannu Valtonen <hannu.valtonen@hut.fi>
 *
 * $PostgreSQL$
 *
 * See the file COPYING for distribution terms.
 */
#ifndef PGMEMCACHE_H
#define PGMEMCACHE_H

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#include <libmemcached/memcached.h>
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

void _PG_init(void);
void _PG_fini(void);

Datum memcache_add(PG_FUNCTION_ARGS);
Datum memcache_add_absexpire(PG_FUNCTION_ARGS);
Datum memcache_decr(PG_FUNCTION_ARGS);
Datum memcache_delete(PG_FUNCTION_ARGS);
Datum memcache_flush_all0(PG_FUNCTION_ARGS);
Datum memcache_get(PG_FUNCTION_ARGS);
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
Datum memcache_stat(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(memcache_add);
PG_FUNCTION_INFO_V1(memcache_add_absexpire);
PG_FUNCTION_INFO_V1(memcache_decr);
PG_FUNCTION_INFO_V1(memcache_delete);
PG_FUNCTION_INFO_V1(memcache_flush_all0);
PG_FUNCTION_INFO_V1(memcache_get);
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
PG_FUNCTION_INFO_V1(memcache_stat);

#endif
