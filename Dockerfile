ARG BASIS=ubuntu:jammy
FROM $BASIS

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    cmake \
    curl \
    g++ \
    make \
    libxerces-c-dev \
    libssl-dev \
    perl-base \
    perl-modules \
    git

ADD . /opt/OpenDDS

RUN cd /opt/OpenDDS && \
    ./configure --prefix=/usr/local/opendds --static --security && \
    ./tools/scripts/show_build_config.pl && \
    make && \
    make install && \
    ldconfig && \
    . /opt/OpenDDS/setenv.sh

ENV ACE_ROOT=/usr/local/opendds/share/ace \
    TAO_ROOT=/usr/local/opendds/share/tao \
    DDS_ROOT=/usr/local/opendds/share/dds 

WORKDIR /root
