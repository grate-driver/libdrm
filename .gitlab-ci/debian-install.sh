#!/usr/bin/env bash
set -o errexit
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y \
  ca-certificates

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update

# Use newer packages from backports by default
cat >/etc/apt/preferences <<EOF
Package: *
Pin: release a=buster-backports
Pin-Priority: 500
EOF

apt-get dist-upgrade -y

apt-get install -y --no-remove \
  build-essential \
  docbook-xsl \
  libatomic-ops-dev \
  libcairo2-dev \
  libcunit1-dev \
  libpciaccess-dev \
  libxslt1-dev \
  ninja-build \
  pkg-config \
  python3 \
  python3-pip \
  python3-wheel \
  python3-setuptools \
  valgrind \
  xsltproc


# Test that the oldest Meson version we claim to support is still supported
pip3 install meson==0.43
