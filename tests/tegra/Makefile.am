AM_CPPFLAGS = \
	-I$(top_srcdir)/include/drm \
	-I$(top_srcdir)/tegra

AM_CFLAGS = -Wall -Werror

LDADD = \
	../../tegra/libdrm_tegra.la

TESTS = \
	openclose \

if HAVE_INSTALL_TESTS
testdir = $(libexecdir)/libdrm/tests/tegra
test_PROGRAMS = \
	$(TESTS)
else
noinst_PROGRAMS = $(TESTS)
check_PROGRAMS = $(TESTS)
endif
