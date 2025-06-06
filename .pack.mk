#
# CI packaging rules
#

SED_REPLACE_VERSION_REGEX = 's/-\([0-9]\+\)-g[0-9a-f]\+$$/.\1/'
RWS_BASE_URL = https://rws.tarantool.org
PRODUCT_NAME = tarantool

VARDIR ?=/tmp/t
OUTPUT_DIR ?=${PWD}/build

GIT_DESCRIBE = $(shell git describe HEAD)
GIT_TAG = $(shell git tag --points-at HEAD)
MAJOR_VERSION = $(word 1, $(subst ., ,${GIT_DESCRIBE}))
MINOR_VERSION = $(word 2, $(subst ., ,${GIT_DESCRIBE}))
VERSION = ${MAJOR_VERSION}.${MINOR_VERSION}

TARANTOOL_SERIES = series-${MAJOR_VERSION}
S3_SOURCE_REPO_URL = s3://tarantool_repo/sources
S3_SCHEMA_REPO_URL = s3://tarantool_repo/schema

prepare:
	rm -rf build packpack
	git clone https://github.com/packpack/packpack.git

package: prepare
	if [ "${OS}" = "alpine" ]; then \
		if [ -n "${GIT_TAG}" ]; then \
			export VERSION="$$(echo ${GIT_TAG} | sed 's/-/_/' | sed 's/entrypoint/alpha0/')"; \
		else \
			RELEASE="$(word 1, $(subst -, ,${GIT_DESCRIBE}))"; \
			TYPE="$(word 2, $(subst -, ,${GIT_DESCRIBE}))"; \
			PATCH="$(word 3, $(subst -, ,${GIT_DESCRIBE}))"; \
			if [ "$${TYPE}" = "entrypoint" ]; then \
				TYPE="alpha0"; \
			fi; \
			export VERSION="$${RELEASE}_$${TYPE}_p$${PATCH}"; \
		fi; \
	else \
		if [ -n "${GIT_TAG}" ]; then \
			export VERSION="$$(echo ${GIT_TAG} | sed 's/-/~/')"; \
		else \
			export VERSION="$$(echo ${GIT_DESCRIBE} | sed ${SED_REPLACE_VERSION_REGEX} | sed 's/-/~/').dev"; \
		fi; \
	fi; \
	echo VERSION=$${VERSION}; \
	PACKPACK_EXTRA_DOCKER_RUN_PARAMS="--network=host --volume ${VARDIR}:${VARDIR} ${PACKPACK_EXTRA_DOCKER_RUN_PARAMS}" \
	TARBALL_EXTRA_ARGS="--exclude=*.exe --exclude=*.dll" \
	PRESERVE_ENVVARS="CMAKE_BUILD_TYPE,TARBALL_EXTRA_ARGS,${PRESERVE_ENVVARS}" \
	./packpack/packpack

package-static:
	if [ -n "${GIT_TAG}" ]; then \
		export VERSION="$$(echo ${GIT_TAG} | sed 's/-/~/')"; \
	else \
		export VERSION="$$(echo ${GIT_DESCRIBE} | sed ${SED_REPLACE_VERSION_REGEX} | sed 's/-/~/').dev"; \
	fi; \
	export OUTPUT_DIR=${OUTPUT_DIR}; \
	echo VERSION=$${VERSION}; \
	echo OUTPUT_DIR=${OUTPUT_DIR}; \
	./static-build/make_packages.sh

deploy:
	if [ -z "${REPO_TYPE}" ]; then \
		echo "Env variable 'REPO_TYPE' must be defined!"; \
		exit 1; \
	fi

	# Use different repos for vanilla and GC64 version.
	if [ "${GC64}" = "true" ]; then \
		RWS_ENDPOINT=${RWS_BASE_URL}/${REPO_TYPE}/${TARANTOOL_SERIES}-gc64/${OS}/${DIST}; \
	else \
		RWS_ENDPOINT=${RWS_BASE_URL}/${REPO_TYPE}/${TARANTOOL_SERIES}/${OS}/${DIST}; \
	fi; \
	CURL_CMD="curl \
		--location \
		--fail \
		--silent \
		--show-error \
		--retry 5 \
		--retry-delay 5 \
		--request PUT $${RWS_ENDPOINT} \
		--user $${RWS_AUTH} \
		--form product=${PRODUCT_NAME}"; \
	for f in $$(ls -I '*build*' -I '*.changes' ./build); do \
		CURL_CMD="$${CURL_CMD} --form $$(basename $${f})=@./build/$${f}"; \
	done; \
	echo $${CURL_CMD}; \
	$${CURL_CMD}

deploy-static:
	if [ -z "${REPO_TYPE}" ]; then \
		echo "Env variable 'REPO_TYPE' must be defined!"; \
		exit 1; \
	fi

	for pkg_type in rpm deb; do \
		RWS_ENDPOINT=${RWS_BASE_URL}/${REPO_TYPE}/${TARANTOOL_SERIES}/linux-$${pkg_type}/static; \
		CURL_CMD="curl \
			--location \
			--fail \
			--silent \
			--show-error \
			--retry 5 \
			--retry-delay 5 \
			--request PUT $${RWS_ENDPOINT} \
			--user $${RWS_AUTH} \
			--form product=${PRODUCT_NAME}"; \
		for f in $$(ls -I '_CPack_Packages' ${OUTPUT_DIR}/*.$${pkg_type}); do \
			CURL_CMD="$${CURL_CMD} --form $$(basename $${f})=@$${f}"; \
		done; \
		echo $${CURL_CMD}; \
		$${CURL_CMD}; \
	done;

source: prepare
	if [ -n "${GIT_TAG}" ]; then \
		export VERSION=${GIT_TAG}; \
	else \
		export VERSION="$$(echo ${GIT_DESCRIBE} | sed ${SED_REPLACE_VERSION_REGEX}).dev"; \
	fi; \
	echo VERSION=$${VERSION}; \
	TARBALL_COMPRESSOR=gz \
	./packpack/packpack tarball

source-deploy: source
	aws --endpoint-url ${AWS_S3_ENDPOINT_URL} s3 cp build/*.tar.gz ${S3_SOURCE_REPO_URL}/ --acl public-read

schema-deploy:
	if [ -n "${GIT_TAG}" ]; then \
		CONFIG_SCHEMA_FILE=config.schema.${GIT_TAG}.json; \
	else \
		CONFIG_SCHEMA_FILE=config.schema.json; \
	fi; \
	mv config-schema-pretty.json $${CONFIG_SCHEMA_FILE}; \
	aws --endpoint-url ${AWS_S3_ENDPOINT_URL} s3 cp $${CONFIG_SCHEMA_FILE} ${S3_SCHEMA_REPO_URL}/ --acl public-read
