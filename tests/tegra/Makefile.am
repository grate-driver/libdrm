AM_CFLAGS = \
	-I $(top_srcdir)/include/drm \
	-I $(top_srcdir)/tegra \
	-I $(top_srcdir)

LDADD = $(top_builddir)/libdrm.la \
	$(top_builddir)/tegra/libdrm_tegra.la


TESTS = host1x gr2d

check_PROGRAMS = $(TESTS)
