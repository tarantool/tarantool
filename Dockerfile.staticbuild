FROM centos:7

RUN yum install -y epel-release
RUN yum install -y yum install https://centos7.iuscommunity.org/ius-release.rpm

RUN set -x \
    && yum -y install \
        libstdc++ \
        libstdc++-static \
        readline \
        openssl \
        lz4 \
        binutils \
        ncurses \
        libgomp \
        lua \
        curl \
        tar \
        zip \
        unzip \
        libunwind \
        libcurl \
    && yum -y install \
        perl \
        gcc-c++ \
        cmake \
        lz4-devel \
        binutils-devel \
        lua-devel \
        make \
        git \
        autoconf \
        automake \
        libtool \
        wget

RUN yum -y install ncurses-static readline-static zlib-static pcre-static glibc-static

RUN set -x && \
    cd / && \
    curl -O -L https://www.openssl.org/source/openssl-1.1.0h.tar.gz && \
    tar -xvf openssl-1.1.0h.tar.gz && \
    cd openssl-1.1.0h && \
    ./config && \
    make && make install

RUN set -x && \
    cd / && \
    git clone https://github.com/curl/curl.git && \
    cd curl && \
    git checkout curl-7_59_0 && \
    ./buildconf && \
    LD_LIBRARY_PATH=/usr/local/lib64 LIBS=" -lssl -lcrypto -ldl" ./configure --enable-static --enable-shared --with-ssl && \
    make -j && make install

RUN set -x && \
    cd / && \
    wget http://download.icu-project.org/files/icu4c/62.1/icu4c-62_1-src.tgz && \
    tar -xvf icu4c-62_1-src.tgz && \
    cd icu/source && \
    ./configure --with-data-packaging=static --enable-static --enable-shared && \
    make && make install

RUN set -x && \
    cd / && \
    LD_LIBRARY_PATH=/usr/local/lib64 curl -O -L http://download.savannah.nongnu.org/releases/libunwind/libunwind-1.3-rc1.tar.gz && \
    tar -xvf libunwind-1.3-rc1.tar.gz && \
    cd libunwind-1.3-rc1 && \
    ./configure --enable-static --enable-shared && \
    make && make install

COPY . /tarantool

RUN set -x && \
    cd tarantool && \
    git submodule init && \
    git submodule update

RUN set -x \
    && (cd /tarantool; \
       cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
             -DENABLE_DIST:BOOL=ON \
             -DBUILD_STATIC=ON \
             -DOPENSSL_USE_STATIC_LIBS=ON \
             .) \
    && make -C /tarantool -j

RUN cd /tarantool && make install

ENTRYPOINT /bin/bash
