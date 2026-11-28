FROM packpack/packpack:centos-7

# CentOS 7 provides the glibc baseline. Its aarch64 archive does not have
# devtoolset-11 GCC, so build it on that architecture.
RUN set -eux; \
    case "$(uname -m)" in \
        x86_64) \
            sed -i 's|http://archive.kernel.org/|https://archive.kernel.org/|g' \
                /etc/yum.repos.d/CentOS-SCLo-scl-rh.repo; \
            yum -y --disablerepo='*' --enablerepo=centos-sclo-rh \
                install devtoolset-11-gcc-c++; \
            yum clean all; \
            ln -s /opt/rh/devtoolset-11/root/usr /opt/tarantool-gcc ;; \
        aarch64) \
            mkdir /tmp/tarantool-gcc; \
            cd /tmp/tarantool-gcc; \
            curl -fsSLO https://gcc.gnu.org/pub/gcc/releases/gcc-11.5.0/gcc-11.5.0.tar.xz; \
            sum=88f17d5a5e69eeb53aaf0a9bc9daab1c4e501d145b388c5485ebeb2cc36178fbb2; \
            sum=${sum}d3e49ebef4a8c007a05e88471a06b97cf9b08870478249f77fbfa3d4abd9a8; \
            echo "$sum  gcc-11.5.0.tar.xz" | sha512sum -c -; \
            tar -xf gcc-11.5.0.tar.xz; \
            sed -i 's|http://gcc.gnu.org/|https://gcc.gnu.org/|' \
                gcc-11.5.0/contrib/download_prerequisites; \
            (cd gcc-11.5.0 && ./contrib/download_prerequisites); \
            mkdir build; \
            cd build; \
            ../gcc-11.5.0/configure --prefix=/opt/tarantool-gcc \
                --disable-bootstrap --disable-multilib --disable-libsanitizer \
                --enable-languages=c,c++; \
            make -j"$(nproc)"; \
            make install; \
            cd /; \
            rm -rf /tmp/tarantool-gcc ;; \
        *) exit 1 ;; \
    esac

ENV PATH=/opt/tarantool-gcc/bin:$PATH \
    CC=/opt/tarantool-gcc/bin/gcc \
    CXX=/opt/tarantool-gcc/bin/g++
