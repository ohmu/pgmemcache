MODULE_big = pgmemcache
OBJS = pgmemcache.o

EXTENSION = pgmemcache
DATA = pgmemcache--2.1.sql pgmemcache--2.0--2.1.sql pgmemcache--unpackaged--2.0.sql

SHLIB_LINK = -lmemcached -lsasl2

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Build a release tarball. To make a release, adjust the
# version number in the README, and add an entry to NEWS.
html:
	rst2html.py README README.html

dist:
	git archive --output=../pgmemcache_$(shell git describe).tar.gz --prefix=pgmemcache/ HEAD .

deb%:
	sed -e s/PGVER/$(subst deb,,$@)/g < debian/packages.in > debian/packages
	sed -e s/PGMCVER/$(shell git describe)/g < debian/changelog.in > debian/changelog
	yada rebuild
	debuild -uc -us -b

build-dep:
	apt-get install libmemcached-dev postgresql-server-dev libpq-dev devscripts yada flex bison libsasl2-dev
