MODULE_big = pgmemcache

OBJS = pgmemcache.o
DATA_built = $(MODULE_big).sql
SHLIB_LINK = -lmemcached -lsasl2

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Build a release tarball. To make a release, adjust the
# version number in the README, and add an entry to NEWS.
html:
	rst2html.py README README.html
dist:
	git archive --format=tar --prefix=pgmemcache/ HEAD . | bzip2 -9 \
	    > ../pgmemcache_$(shell git describe).tar.bz2
deb%:
	sed -e s/PGVER/$(subst deb,,$@)/g < debian/packages.in > debian/packages
	sed -e s/PGMCVER/$(shell git describe)/g < debian/changelog.in > debian/changelog
	yada rebuild
	debuild -uc -us -b
build-dep:
	apt-get install libmemcached-dev postgresql-server-dev libpq-dev devscripts yada flex bison libsasl2-dev
