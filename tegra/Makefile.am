AM_CFLAGS = \
	$(WARN_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/tegra \
	$(PTHREADSTUBS_CFLAGS) \
	-I$(top_srcdir)/include/drm

libdrm_tegra_la_LTLIBRARIES = libdrm_tegra.la
libdrm_tegra_ladir = $(libdir)
libdrm_tegra_la_LDFLAGS = -version-number 1:0:0 -no-undefined
libdrm_tegra_la_LIBADD = ../libdrm.la @PTHREADSTUBS_LIBS@

libdrm_tegra_la_SOURCES = \
	tegra_drm.c

libdrm_tegracommonincludedir = ${includedir}/tegra
libdrm_tegracommoninclude_HEADERS = \
	tegra_drm.h

libdrm_tegraincludedir = ${includedir}/libdrm
libdrm_tegrainclude_HEADERS = \
	tegra_drmif.h

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = libdrm_tegra.pc
