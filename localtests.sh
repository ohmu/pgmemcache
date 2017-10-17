#!/bin/bash -xue

# Create a clean PostgreSQL cluster for our testing

TESTDIR="$(pwd)/regressiondata"
export PGPORT=$((10240 + RANDOM / 2))
export PGDATA="$TESTDIR/pg"

rm -rf "$TESTDIR"
mkdir -p "$PGDATA"

initdb -E UTF-8 --no-locale
sed -e "s%^#port =.*%port = $PGPORT%" \
    -e "s%^#\(unix_socket_director[a-z]*\) =.*%\1 = '$PGDATA'%" \
    -e "s%^#dynamic_library_path = .*%dynamic_library_path = '$(pwd):\$libdir'%" \
    -e "s%^#fsync = .*%fsync = off%" \
    -e "s%^#synchronous_commit = .*%synchronous_commit = off%" \
    -i "$PGDATA/postgresql.conf"
pg_ctl -l "$PGDATA/logfile" start
while [ ! -S "$PGDATA/.s.PGSQL.$PGPORT" ]; do sleep 2; done
trap "pg_ctl stop -m immediate" EXIT

# It's not possible to override the extension path, so we'll just execute
# the extension SQL directly after mangling it a bit with sed

cp -a Makefile test.sql sql/ expected/ "$TESTDIR"
sed -e "s%MODULE_PATHNAME%pgmemcache%" \
    -e "/CREATE EXTENSION/d" -e "/^--/d" -e "/^$/d" \
    "ext/pgmemcache.sql" > "$TESTDIR/sql/init.sql"
cp "$TESTDIR/sql/init.sql" "$TESTDIR/expected/init.out"

# Run the actual tests

make -C "$TESTDIR" installcheck REGRESS_OPTS="--host=$PGDATA --port=$PGPORT"
