FROM ubuntu:22.04

ARG TARANTOOL_VERSION
ARG TARANTOOL_VERSION_REGEX="^([0-9]+\.[0-9]+\.[0-9]+)($|-(alpha|beta|rc)[1-9]$)"
ARG TARANTOOL_DOWNLOAD_BASE_URL="https://download.tarantool.org/tarantool"
ARG TARANTOOL_APT_SOURCES_FILE="/etc/apt/sources.list.d/tarantool.list"

ENV TT_APP_NAME="default"
ENV TT_INSTANCE_NAME="master"

RUN if [ -z "${TARANTOOL_VERSION}" ]; then \
        echo "ERROR: TARANTOOL_VERSION is not defined"; \
        echo "ERROR: Please provide, for example, --build-arg TARANTOOL_VERSION=3.1.0"; \
        exit 1; \
    elif [ ! $(echo "${TARANTOOL_VERSION}" | grep -E "${TARANTOOL_VERSION_REGEX}") ]; then \
        echo "ERROR: TARANTOOL_VERSION is not valid"; \
        echo "ERROR: Please provide, for example, 3.1.0 or 3.1.0-(alpha|beta|rc)(1-9)"; \
        exit 1; \
    else \
        echo "TARANTOOL_VERSION is defined and valid"; \
    fi

RUN useradd --system --user-group --shell /bin/sh tarantool

RUN apt-get update && \
    apt-get -y --no-install-recommends install \
        gosu \
        curl \
        gnupg2 \
        ca-certificates && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN case "${TARANTOOL_VERSION}" in \
        *-alpha*|*-beta*|*-rc*) \
            REPO_TYPE="pre-release" \
            TARANTOOL_VERSION="$(echo "${TARANTOOL_VERSION}" | sed 's/-/~/')" \
            ;; \
        *) \
            REPO_TYPE="release" \
            ;; \
    esac && \
    TARANTOOL_SERIES="series-$(echo "${TARANTOOL_VERSION}" | cut -d . -f 1)" && \
    UBUNTU_CODENAME="$(. /etc/os-release && echo "${UBUNTU_CODENAME}")" && \
    curl -L ${TARANTOOL_DOWNLOAD_BASE_URL}/${REPO_TYPE}/${TARANTOOL_SERIES}/gpgkey | apt-key add - && \
    curl -L ${TARANTOOL_DOWNLOAD_BASE_URL}/modules/gpgkey | apt-key add - && \
    echo "deb ${TARANTOOL_DOWNLOAD_BASE_URL}/${REPO_TYPE}/${TARANTOOL_SERIES}/linux-deb/ static main" >> ${TARANTOOL_APT_SOURCES_FILE} && \
    echo "deb ${TARANTOOL_DOWNLOAD_BASE_URL}/release/modules/linux-deb/ static main" >> ${TARANTOOL_APT_SOURCES_FILE} && \
    echo "deb ${TARANTOOL_DOWNLOAD_BASE_URL}/release/modules/ubuntu/ ${UBUNTU_CODENAME} main" >> ${TARANTOOL_APT_SOURCES_FILE} && \
    apt-get update && \
    apt-get -y --no-install-recommends install \
        tarantool="${TARANTOOL_VERSION}-1" \
        tarantool-dev="${TARANTOOL_VERSION}-1" \
        tt && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/tarantool && \
    mkdir -p /var/lib/tarantool && \
    mkdir -p /var/log/tarantool && \
    mkdir -p /var/run/tarantool && \
    chown -R tarantool:tarantool /opt/tarantool && \
    chown -R tarantool:tarantool /var/lib/tarantool && \
    chown -R tarantool:tarantool /var/log/tarantool && \
    chown -R tarantool:tarantool /var/run/tarantool

WORKDIR /opt/tarantool
VOLUME /var/lib/tarantool

COPY config/default-config.yaml /
COPY entrypoint/docker-entrypoint.sh /
COPY tools/console /usr/local/bin/
COPY tools/status /usr/local/bin/

EXPOSE 3301

HEALTHCHECK CMD status

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["tarantool"]
