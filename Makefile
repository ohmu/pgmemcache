MODULE_big = pgmemcache
PGMC_VERSION = 2.0.5

OBJS = pgmemcache.o
DATA_built = $(MODULE_big).sql
SHLIB_LINK = -lmemcached -lsasl2

PGXS := $(shell pg_config --pgxs)
include $(PGXS)

# Build a release tarball. To make a release, update PGMC_VERSION, adjust 
# the version number in the README, and add an entry to NEWS.
html:
	rst2html.py README.pgmemcache README.pgmemcache.html
dist:
	tar -cjf ../pgmemcache_$(PGMC_VERSION).tar.bz2 ../pgmemcache/
deb84:
	sed -e s/PGVER/8.4/g < debian/packages.in > debian/packages
	yada rebuild
	debuild -uc -us -b
deb90:
	sed -e s/PGVER/9.0/g < debian/packages.in > debian/packages
	yada rebuild
	debuild -uc -us -b

