
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

#ifndef PGMEMCACHE_H
#define PGMEMCACHE_H

#include <memcache.h>

Datum memcache_add(PG_FUNCTION_ARGS);
Datum memcache_add_absexpire(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_add);
PG_FUNCTION_INFO_V1(memcache_add_absexpire);

Datum memcache_decr(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_decr);

Datum memcache_delete(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_delete);

Datum memcache_flush(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_flush);

Datum memcache_flush_all0(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_flush_all0);

Datum memcache_free(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_free);

Datum memcache_get(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_get);

Datum memcache_hash(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_hash);

Datum memcache_incr(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_incr);

Datum memcache_init(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_init);

Datum memcache_replace(PG_FUNCTION_ARGS);
Datum memcache_replace_absexpire(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_replace);
PG_FUNCTION_INFO_V1(memcache_replace_absexpire);

Datum memcache_server_add(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_server_add);

Datum memcache_set(PG_FUNCTION_ARGS);
Datum memcache_set_absexpire(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_set);
PG_FUNCTION_INFO_V1(memcache_set_absexpire);

Datum memcache_stats(PG_FUNCTION_ARGS);
Datum memcache_stat(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_stats);
PG_FUNCTION_INFO_V1(memcache_stat);


/* DEPRECIATED INTERFACES */

/* Use memcache_flush() */
Datum memcache_flush_all(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(memcache_flush_all);

#endif
