GITLAB_MAKE:=${MAKE} -f .gitlab.mk
TRAVIS_MAKE:=${MAKE} -f .travis.mk

# Pass *_no_deps goals to .travis.mk.
test_%:
	${TRAVIS_MAKE} $@

# #######################################################
# Build and push testing docker images to GitLab Registry
# #######################################################

# These images contains tarantool dependencies and testing
# dependencies to run tests in them.
#
# How to run:
#
# make DOCKER_USER=foo -f .gitlab.mk docker_bootstrap
#
# The command will prompt for a password. If two-factor
# authentication is enabled an access token with 'api' scope
# should be entered here instead of a password.
#
# When to run:
#
# When some of deps_* goals in .travis.mk are updated.
#
# Keep in a mind that the resulting image is used to run tests on
# all branches, so avoid removing packages: only add them.

DOCKER_REGISTRY?=docker.io
DOCKER_BUILD=docker build --network=host -f - .

define DEBIAN_STRETCH_DOCKERFILE
FROM packpack/packpack:debian-stretch
COPY .travis.mk .
RUN make -f .travis.mk deps_debian
endef
export DEBIAN_STRETCH_DOCKERFILE

define DEBIAN_BUSTER_DOCKERFILE
FROM packpack/packpack:debian-buster
COPY .travis.mk .
RUN make APT_EXTRA_FLAGS="--allow-releaseinfo-change-version --allow-releaseinfo-change-suite" -f .travis.mk deps_buster_clang_8 deps_buster_clang_11
endef
export DEBIAN_BUSTER_DOCKERFILE

IMAGE_PREFIX:=${DOCKER_REGISTRY}/tarantool/testing
DEBIAN_STRETCH_IMAGE:=${IMAGE_PREFIX}:debian-stretch
DEBIAN_BUSTER_IMAGE:=${IMAGE_PREFIX}:debian-buster

TRAVIS_CI_MD5SUM:=$(firstword $(shell md5sum .travis.mk))

docker_bootstrap:
	# Login.
	docker login -u ${DOCKER_USER} ${DOCKER_REGISTRY}
	# Build images.
	echo "$${DEBIAN_STRETCH_DOCKERFILE}" | ${DOCKER_BUILD} \
		-t ${DEBIAN_STRETCH_IMAGE}_${TRAVIS_CI_MD5SUM} \
		-t ${DEBIAN_STRETCH_IMAGE}
	echo "$${DEBIAN_BUSTER_DOCKERFILE}" | ${DOCKER_BUILD} \
		-t ${DEBIAN_BUSTER_IMAGE}_${TRAVIS_CI_MD5SUM} \
		-t ${DEBIAN_BUSTER_IMAGE}
	# Push images.
	docker push ${DEBIAN_STRETCH_IMAGE}_${TRAVIS_CI_MD5SUM}
	docker push ${DEBIAN_BUSTER_IMAGE}_${TRAVIS_CI_MD5SUM}
	docker push ${DEBIAN_STRETCH_IMAGE}
	docker push ${DEBIAN_BUSTER_IMAGE}

# Clone the benchmarks repository for performance testing
perf_clone_benchs_repo:
	git clone https://github.com/tarantool/bench-run.git

# Build images for performance testing
perf_prepare: perf_clone_benchs_repo
	make -f bench-run/targets.mk prepare

# Remove temporary performance image
perf_cleanup_image:
	make -f bench-run/targets.mk cleanup

# Remove temporary performance image from the test host
perf_cleanup: perf_clone_benchs_repo perf_cleanup_image

# ######
# Deploy
# ######

GIT_DESCRIBE=$(shell git describe HEAD)
MAJOR_VERSION=$(word 1,$(subst ., ,$(GIT_DESCRIBE)))
MINOR_VERSION=$(word 2,$(subst ., ,$(GIT_DESCRIBE)))
VERSION=$(MAJOR_VERSION).$(MINOR_VERSION)

ifeq ($(VERSION), $(filter $(VERSION), 1.10 2.8))
TARANTOOL_SERIES=$(MAJOR_VERSION).$(MINOR_VERSION)
S3_SOURCE_REPO_URL=s3://tarantool_repo/sources/$(TARANTOOL_SERIES)
else
TARANTOOL_SERIES=series-$(MAJOR_VERSION)
S3_SOURCE_REPO_URL=s3://tarantool_repo/sources
endif

RWS_BASE_URL=https://rws.tarantool.org
RWS_ENDPOINT=${RWS_BASE_URL}/${REPO_TYPE}/${TARANTOOL_SERIES}/${OS}/${DIST}
PRODUCT_NAME=tarantool

deploy_prepare:
	[ -d packpack ] || \
		git clone https://github.com/packpack/packpack.git packpack
	rm -rf build

package: deploy_prepare
	PACKPACK_EXTRA_DOCKER_RUN_PARAMS="--network=host ${PACKPACK_EXTRA_DOCKER_RUN_PARAMS}" ./packpack/packpack

deploy:
	if [ -z "${REPO_TYPE}" ]; then \
		echo "Env variable 'REPO_TYPE' must be defined!"; \
		exit 1; \
	fi; \
	CURL_CMD="curl \
		--location \
		--fail \
		--silent \
		--show-error \
		--retry 5 \
		--retry-delay 5 \
		--request PUT ${RWS_ENDPOINT} \
		--user $${RWS_AUTH} \
		--form product=${PRODUCT_NAME}"; \
	for f in $$(ls -I '*build*' -I '*.changes' ./build); do \
		CURL_CMD="$${CURL_CMD} --form $$(basename $${f})=@./build/$${f}"; \
	done; \
	echo $${CURL_CMD}; \
	$${CURL_CMD}

source: deploy_prepare
	TARBALL_COMPRESSOR=gz packpack/packpack tarball

source_deploy: source
	( aws --endpoint-url "${AWS_S3_ENDPOINT_URL}" s3 ls "${S3_SOURCE_REPO_URL}/" || \
		( rm -rf "${TARANTOOL_SERIES}" ; mkdir "${TARANTOOL_SERIES}" &&                        \
			aws --endpoint-url "${AWS_S3_ENDPOINT_URL}"                \
				s3 mv "${TARANTOOL_SERIES}" "${S3_SOURCE_REPO_URL}" --recursive   \
				--acl public-read ) ) &&                           \
		aws --endpoint-url "${AWS_S3_ENDPOINT_URL}"                        \
			s3 cp build/*.tar.gz "${S3_SOURCE_REPO_URL}/" --acl public-read

# ###################
# Performance testing
# ###################

perf_run:
	/opt/bench-run/benchs/${BENCH}/run.sh ${ARG}
