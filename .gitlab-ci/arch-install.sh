#!/usr/bin/env bash
set -o errexit
set -o xtrace

pacman -Syu --noconfirm --needed \
  base-devel \
  cairo \
  cunit \
  docbook-xsl \
  libatomic_ops \
  libpciaccess \
  libxslt \
  meson \
  valgrind
