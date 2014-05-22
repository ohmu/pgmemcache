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
