#
# $Id$
#

SRCS		+= pgmemcache.c
MODULE_big	= pgmemcache
OBJS		= $(SRCS:.c=.o)
DOCS		= README.pgmemcache
DATA_built	= pgmemcache.sql
LIBMEMCACHEPREFIX = /usr/local

PG_CPPFLAGS	= -I$(LIBMEMCACHEPREFIX)/include -I$(srcdir)
SHLIB_LINK 	= -L$(LIBMEMCACHEPREFIX)/lib -lmemcache

ifdef USE_PGXS
PGXS = $(shell pg_config --pgxs)
include $(PGXS)
else
subdir = contrib/pgmemcache
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
