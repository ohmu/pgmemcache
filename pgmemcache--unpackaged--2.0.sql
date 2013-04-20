-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pgmemcache" to load this file. \quit

ALTER EXTENSION pgmemcache ADD FUNCTION memcache_server_add(server_hostname text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_stats();
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_flush_all();
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_delete(key text, hold interval);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_delete(key text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_get(key text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_get(key bytea);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_get_multi(IN keys text[], OUT key text, OUT value text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_get_multi(IN keys bytea[], OUT key text, OUT value text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_set(key text, val text, expire timestamptz);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_set(key text, val text, expire interval);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_set(key text, val text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_set(key bytea, val text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_set(key text, val bytea);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_add(key text, val text, expire timestamptz);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_add(key text, val text, expire interval);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_add(key text, val text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_replace(key text, val text, expire timestamptz);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_replace(key text, val text, expire interval);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_replace(key text, val text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_append(key text, val text, expire timestamptz);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_append(key text, val text, expire interval);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_append(key text, val text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_prepend(key text, val text, expire timestamptz);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_prepend(key text, val text, expire interval);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_prepend(key text, val text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_incr(key text, increment int);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_incr(key text);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_decr(key text, decrement int);
ALTER EXTENSION pgmemcache ADD FUNCTION memcache_decr(key text);
