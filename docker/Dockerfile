FROM ubuntu:22.04

ARG TARANTOOL_VERSION

ENV DEBIAN_FRONTEND=noninteractive

RUN set -x \
    && useradd --user-group --create-home --no-log-init --shell /bin/bash tarantool \
    && apt update && apt -y --no-install-recommends --no-install-suggests install \
    gcc \
    git \
    gosu \
    curl \
    wget \
    unzip \
    gnupg2 \
    lsb-release \
    liblua5.1-dev \
    ca-certificates \
    && apt clean

RUN set -x \
    && case $TARANTOOL_VERSION} in \
        *-alpha*|*-beta*|*-rc*) \
            REPO_TYPE=pre-release \
            TARANTOOL_VERSION=$(echo $TARANTOOL_VERSION | sed 's/-/~/') \
            ;; \
        *) \
            REPO_TYPE=release \
            ;; \
        esac \
    && curl -L https://download.tarantool.org/tarantool/${REPO_TYPE}/series-3/gpgkey | apt-key add - \
    && curl -L https://download.tarantool.org/tarantool/modules/gpgkey | apt-key add - \
    && echo "deb https://download.tarantool.org/tarantool/${REPO_TYPE}/series-3/linux-deb/ static main" >> /etc/apt/sources.list.d/tarantool.list \
    && echo "deb-src https://download.tarantool.org/tarantool/${REPO_TYPE}/series-3/linux-deb/ static main" >> /etc/apt/sources.list.d/tarantool.list \
    && echo "deb https://download.tarantool.org/tarantool/release/modules/linux-deb/ static main" >> /etc/apt/sources.list.d/tarantool.list \
    && echo "deb-src https://download.tarantool.org/tarantool/release/modules/linux-deb/ static main" >> /etc/apt/sources.list.d/tarantool.list \
    && apt update \
    && apt show -a tarantool \
    && apt -y --no-install-recommends --no-install-suggests install \
    tarantool=${TARANTOOL_VERSION}-1 \
    tarantool-dev=${TARANTOOL_VERSION}-1 \
    tt \
    && tt rocks install lua-term \
    && apt clean \
    && apt -y autoremove \
    && rm -rf /var/lib/apt/lists/ \
    && mkdir -p /etc/tarantool \
    && mkdir -p /var/lib/tarantool \
    && mkdir -p /var/run/tarantool \
    && chown -R tarantool:tarantool /etc/tarantool \
    && chown -R tarantool:tarantool /var/lib/tarantool \
    && chown -R tarantool:tarantool /var/run/tarantool

COPY files/tarantool-entrypoint.lua /usr/local/bin/
COPY files/tarantool_set_config.lua /usr/local/bin/
COPY files/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
COPY files/console /usr/local/bin/
COPY files/tarantool_is_up /usr/local/bin/

RUN ln -s /usr/local/bin/docker-entrypoint.sh /entrypoint.sh

ENTRYPOINT ["docker-entrypoint.sh"]

HEALTHCHECK CMD tarantool_is_up

EXPOSE 3301
CMD [ "tarantool" ]
