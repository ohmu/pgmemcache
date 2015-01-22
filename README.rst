pgmemcache
==========

pgmemcache is a set of PostgreSQL user-defined functions that provide an
interface to memcached.  Installing pgmemcache is easy, but does have a few
trivial requirements.

Requirements
============

* PostgreSQL_ 9.1 or newer
* libmemcached_ 0.38 or OMcache_ 0.2.0 or newer

.. _PostgreSQL: http://www.postgresql.org/
.. _libmemcached: http://libmemcached.org/
.. _OMcache: https://github.com/ohmu/omcache

Always prefer the latest versions.

pgmemcache uses libmemcached by default, but support for an alternative
memcache client library, OMcache, was added during pgmemcache 2.2.x
development.  pgmemcache can be built with OMcache instead of libmemcached
by passing USE_OMCACHE=1 argument to make.

pgmemcache uses the memcache binary protocol by default, this is required
for the "increment / decrement with initial" operations pgmemcache uses.

Installation
============

Building from sources
---------------------

1) Install PostgreSQL with PGXS (if not already installed)
2) Install libmemcached_ or OMcache_
3) make
4) sudo make install

If necessary, update LD_LIBRARY_CACHE for the PostgreSQL server owner and restart PostgreSQL.
Also make sure that the pg_config binary is in your PATH (or edit the Makefile).

Building on Debian
------------------

1) sudo make build-dep
2) make deb9.2  (or deb9.3, deb9.4, etc depending on your PostgreSQL version)
3) sudo dpkg -i ../postgresql-pgmemcache-*.deb

Building on Fedora, CentOS or RHEL
----------------------------------

1) make rpm
2) sudo rpm -Uvh $(rpm --eval '%{_rpmdir}/%{_arch}')/pgmemcache-*.rpm

Setup for PostgreSQL
====================

::

    % psql <mydbname> -c 'create extension pgmemcache'

It is often more convenient to specify a list of memcached servers
to connect to in postgresql.conf, rather than calling memcache_server_add()
in each new client connection. This can be done as follows:

1. Edit postgresql.conf
2. Append "pgmemcache" to shared_preload_libraries

(If using PostgreSQL 9.1 or earlier versions)

3. Append "pgmemcache" to custom_variable_classes

(Optional parts)

4. Set the "pgmemcache.default_servers" custom GUC variable to a
   comma-separated list of 'host:port' pairs (the port is optional).
5. Set the pgmemcache.default_behavior flags to suit your needs. The format is a
   comma-separated list of memcached behavior (optional) of behavior_flag:behavior_data.
   The flags correspond with libmemcached behavior flags. Check the libmemcached
   documentation for those.
   As an example behavior you might have the following line in your postgresql.conf::

    pgmemcache.default_behavior='DEAD_TIMEOUT:2'

In case your system has SELinux please install the required SELinux policy::

    /usr/bin/checkmodule -M -m -o pgmemcache.mod pgmemcache.te
    /usr/bin/semodule_package -o pgmemcache.pp -m pgmemcache.mod
    /usr/sbin/semodule -i pgmemcache.pp

Usage
=====

::

    memcache_server_add('hostname:port'::TEXT)
    memcache_server_add('hostname'::TEXT)

Adds a server to the list of available servers. If the port is not specified,
the memcached default port (11211) is used. This should only be done in one
central place in the code (normally wrapped in an IF statement).

::

    memcache_add(key::TEXT, value::TEXT, expire::TIMESTAMPTZ)
    memcache_add(key::TEXT, value::TEXT, expire::INTERVAL)
    memcache_add(key::TEXT, value::TEXT)

Adds a key to the cache cluster, if the key does not already exist.

::

    newval = memcache_decr(key::TEXT, decrement::INT8)
    newval = memcache_decr(key::TEXT)

If key exists and is an integer, atomically decrements by the value specified
(default decrement is one).  Returns INT value after decrement.

::

    memcache_delete(key::TEXT, hold_timer::INTERVAL)
    memcache_delete(key::TEXT)

Deletes a given key. If a hold timer is specified, key with the same name can't
be added until the hold timer expires.

::

    memcache_flush_all()

Flushes (drops) all data on all servers in the memcache cluster.

::

    value = memcache_get(key::TEXT)

Fetches a key out of the cache. Returns NULL if the key does not exist; otherwise,
it returns the value of the key as TEXT. Note that zero-length values are allowed.

::

    memcache_get_multi(keys::TEXT[])
    memcache_get_multi(keys::BYTEA[])

    SELECT key, value FROM memcache_get_multi('{qwerty,asdfg}'::TEXT[]);


Fetches an ARRAY of keys from the cache, returns a list of RECORDs
for the found keys, with the columns titled key and value.

::

    newval = memcache_incr(key::TEXT, increment::INT8)
    newval = memcache_incr(key::TEXT)

If key exists and is an integer, atomically increment by the value specified
(the default increment is one).  Returns INT value after increment.

::

    memcache_replace(key::TEXT, value::TEXT, expire::TIMESTAMPTZ)
    memcache_replace(key::TEXT, value::TEXT, expire::INTERVAL)
    memcache_replace(key::TEXT, value::TEXT)

Replaces an existing key's value if the key already exists.

::

    memcache_set(key::TEXT, value::TEXT, expire::TIMESTAMPTZ)
    memcache_set(key::TEXT, value::TEXT, expire::INTERVAL)
    memcache_set(key::TEXT, value::TEXT)

Regardless of whether the specified key already exists, set its
current value to "value", replacing the previous value if any.

::

   stats = memcache_stats()

Returns a TEXT string with all of the stats from all servers in the server list.

Examples
========

Most installations will need a few functions to allow pgmemcache to work correctly.
Here are a few example functions that should get most people off the ground and running.

The following function is an example of a trigger function that is used to
replace the value of something in the cache with its new value.
::

    CREATE OR REPLACE FUNCTION auth_passwd_upd()
    RETURNS TRIGGER LANGUAGE plpgsql AS $$
    BEGIN
        IF OLD.passwd <> NEW.passwd THEN
            PERFORM memcache_replace('user_id_' || NEW.user_id || '_password', NEW.passwd);
        END IF;
        RETURN NEW;
    END;
    $$;

Activate the trigger for UPDATEs::

    CREATE TRIGGER auth_passwd_upd_trg AFTER UPDATE ON passwd
        FOR EACH ROW EXECUTE PROCEDURE auth_passwd_upd();

The above is not transaction safe, however.  A better approach is to have pgmemcache
invalidate the cached data, but not replace it.

::

    CREATE OR REPLACE FUNCTION auth_passwd_upd()
    RETURNS TRIGGER LANGUAGE plpgsql AS $$
    BEGIN
        IF OLD.passwd <> NEW.passwd THEN
            PERFORM memcache_delete('user_id_' || NEW.user_id || '_password');
        END IF;
        RETURN NEW;
    END;
    $$;

Here's an example delete trigger::

    CREATE OR REPLACE FUNCTION auth_passwd_del()
    RETURNS TRIGGER LANGUAGE plpgsql AS $$
    BEGIN
        PERFORM memcache_delete('user_id_' || OLD.user_id || '_password');
        RETURN OLD;
    END;
    $$;

Activate the trigger for DELETEs::

    CREATE TRIGGER auth_passwd_del_trg AFTER DELETE ON passwd
        FOR EACH ROW EXECUTE PROCEDURE auth_passwd_del();

License
=======

pgmemcache is released under an MIT-style license (BSD without advertising
clause).  For the exact license terms, see the file "LICENSE".

Contact
=======

pgmemcache is currently maintained by Hannu Valtonen and Oskari Saarenmaa
from Ohmu Ltd, they can be contacted at <pgmemcache@ohmu.fi>.

Bug reports and patches are very welcome; issues should be reported in
GitHub's issue interface (https://github.com/ohmu/pgmemcache) and patches
and other enhancement proposals should be submitted as GitHub pull requests.

Credits
=======

pgmemcache was originally written by Sean Chittenden.  Version 1.x series
was maintained by Neil Conway and sponsored by the Open Technology Group,
Inc.  Version 2.0 was rewritten to work on top of libmemcached and the
maintainership moved to Hannu Valtonen.

Suzuki Hironobu contributed major patches for the 2.0 series, among other
things, support for libmemcached configuration settings.  F-Secure
Corporation contributed extension support and bug fixes for version 2.1.

See https://github.com/ohmu/pgmemcache/graphs/contributors for the list of
recent contributors.
