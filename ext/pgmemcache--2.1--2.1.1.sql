\echo Use "ALTER EXTENSION pgmemcache UPDATE TO '2.1.1'" to load this file. \quit

DROP FUNCTION memcache_incr(key text, increment int);
CREATE FUNCTION memcache_incr(key text, increment bigint)
RETURNS bigint
AS 'MODULE_PATHNAME', 'memcache_incr'
LANGUAGE c STRICT;

DROP FUNCTION memcache_incr(key text);
CREATE FUNCTION memcache_incr(key text)
RETURNS bigint
AS 'MODULE_PATHNAME', 'memcache_incr'
LANGUAGE c STRICT;

DROP FUNCTION memcache_decr(key text, decrement int);
CREATE FUNCTION memcache_decr(key text, decrement bigint)
RETURNS bigint
AS 'MODULE_PATHNAME', 'memcache_decr'
LANGUAGE c STRICT;

DROP FUNCTION memcache_decr(key text);
CREATE FUNCTION memcache_decr(key text)
RETURNS bigint
AS 'MODULE_PATHNAME', 'memcache_decr'
LANGUAGE c STRICT;
