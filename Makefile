#$PostgreSQL$
MODULE_big = pgmemcache
PGMC_VERSION = 2.0beta2

OBJS = pgmemcache.o
DATA_built = $(MODULE_big).sql
SHLIB_LINK = -lmemcached

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

# Build a release tarball. "hg archive" includes a single HG metadata
# for some reason, so we have to manually exclude it. To make a release,
# update PGMC_VERSION, adjust the version number in the README, and
# add an entry to NEWS.
dist:
	tar -cjf ../pgmemcache_$(PGMC_VERSION).tar.bz2 ../pgmemcache/
