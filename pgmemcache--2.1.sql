-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgmemcache" to load this file. \quit

CREATE FUNCTION memcache_server_add(server_hostname text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_server_add'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_stats()
RETURNS text
AS 'MODULE_PATHNAME', 'memcache_stats'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_flush_all()
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_flush_all0'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_delete(key text, hold interval)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_delete'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_delete(key text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_delete'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_get(key text)
RETURNS text
AS 'MODULE_PATHNAME', 'memcache_get'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_get(key bytea)
RETURNS text
AS 'MODULE_PATHNAME', 'memcache_get'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_get_multi(IN keys text[], OUT key text, OUT value text)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'memcache_get_multi'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_get_multi(IN keys bytea[], OUT key text, OUT value text)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'memcache_get_multi'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_set(key text, val text, expire timestamptz)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_set_absexpire'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_set(key text, val text, expire interval)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_set'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_set(key text, val text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_set'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_set(key bytea, val text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_set'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_set(key text, val bytea)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_set'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_add(key text, val text, expire timestamptz)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_add_absexpire'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_add(key text, val text, expire interval)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_add'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_add(key text, val text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_add'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_replace(key text, val text, expire timestamptz)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_replace_absexpire'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_replace(key text, val text, expire interval)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_replace'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_replace(key text, val text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_replace'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_append(key text, val text, expire timestamptz)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_append_absexpire'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_append(key text, val text, expire interval)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_append'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_append(key text, val text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_append'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_prepend(key text, val text, expire timestamptz)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_prepend_absexpire'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_prepend(key text, val text, expire interval)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_prepend'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_prepend(key text, val text)
RETURNS bool
AS 'MODULE_PATHNAME', 'memcache_prepend'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_incr(key text, increment int)
RETURNS int
AS 'MODULE_PATHNAME', 'memcache_incr'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_incr(key text)
RETURNS int
AS 'MODULE_PATHNAME', 'memcache_incr'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_decr(key text, decrement int)
RETURNS int
AS 'MODULE_PATHNAME', 'memcache_decr'
LANGUAGE c STRICT;

CREATE FUNCTION memcache_decr(key text)
RETURNS int
AS 'MODULE_PATHNAME', 'memcache_decr'
LANGUAGE c STRICT;
