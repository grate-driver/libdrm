AM_CFLAGS = \
	-I $(top_srcdir)/include/drm \
	-I $(top_srcdir)/tegra \
	-I $(top_srcdir)

LDADD = $(top_builddir)/libdrm.la \
	$(top_builddir)/tegra/libdrm_tegra.la

if HAVE_INSTALL_TESTS
bin_PROGRAMS = gr2d
else
noinst_PROGRAMS = gr2d
endif

TESTS = gr2d

check_PROGRAMS = $(TESTS)
