#!/usr/bin/bash

# Script performs setup of test environment using Terraform,
# runs Jepsen tests and teardown test environment.
# Script expects following environment variables:
# 	TF_VAR_user_name
# 	TF_VAR_password
# 	TF_VAR_tenant_id
# 	TF_VAR_user_domain_id
# 	TF_VAR_keypair_name
# 	TF_VAR_ssh_key
# and three arguments: path to a Tarantool project source directory, path to a
# Tarantool project binary directory and number on instances (optional).

set -Eeo pipefail

PROJECT_SOURCE_DIR=$1
PROJECT_BINARY_DIR=$2
INSTANCE_COUNT=$3

TESTS_DIR="$PROJECT_BINARY_DIR/jepsen-tests-prefix/src/jepsen-tests/"
TERRAFORM_CONFIG="$PROJECT_SOURCE_DIR/extra/tf"
TERRAFORM_STATE="$PROJECT_BINARY_DIR/terraform.tfstate"
SSH_KEY_FILENAME="$PROJECT_SOURCE_DIR/build/tf-cloud-init"
NODES_FILENAME="$PROJECT_SOURCE_DIR/build/nodes"

LEIN_OPTIONS="--nodes-file $NODES_FILENAME --username ubuntu --workload register"

[[ -z ${PROJECT_SOURCE_DIR} ]] && (echo "Please specify path to a project source directory"; exit 1)
[[ -z ${PROJECT_BINARY_DIR} ]] && (echo "Please specify path to a project binary directory"; exit 1)
[[ -z ${TF_VAR_ssh_key} ]] && (echo "Please specify TF_VAR_ssh_key env var"; exit 1)
[[ -z ${TF_VAR_keypair_name} ]] && (echo "Please specify TF_VAR_keypair_name env var"; exit 1)

TERRAFORM_BIN=$(which terraform)
LEIN_BIN=$(which lein)
CLOJURE_BIN=$(which clojure)

[[ -z ${TERRAFORM_BIN} ]] && (echo "terraform is not installed"; exit 1)
[[ -z ${LEIN_BIN} ]] && (echo "lein is not installed"; exit 1)
[[ -z ${CLOJURE_BIN} ]] && (echo "clojure is not installed"; exit 1)

function cleanup {
    echo "cleanup"
    rm -f $NODES_FILENAME $SSH_KEY_FILENAME
    [[ -e $TERRAFORM_STATE ]] && \
	terraform destroy -state=$TERRAFORM_STATE -auto-approve $TERRAFORM_CONFIG
}

trap "{ cleanup; exit 255; }" SIGINT SIGTERM ERR

echo -e "${TF_VAR_ssh_key//_/\\n}" > $SSH_KEY_FILENAME
chmod 400 $SSH_KEY_FILENAME
$(pgrep ssh-agent 2>&1 > /dev/null) || eval "$(ssh-agent)"
ssh-add $SSH_KEY_FILENAME

if [[ -z ${CI_JOB_ID} ]]; then
    TF_VAR_id=$CI_JOB_ID
else
    RANDOM_ID=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 13; echo '')
    TF_VAR_id=TF-$RANDOM_ID
fi

export TF_VAR_id
export TF_VAR_ssh_key_path=$SSH_KEY_FILENAME
[[ -z ${INSTANCE_COUNT} ]] && TF_VAR_instance_count=1 || TF_VAR_instance_count=$INSTANCE_COUNT
export TF_VAR_instance_count

[[ -e $HOME/.ssh ]] || (mkdir $HOME/.ssh && touch $HOME/.ssh/known_hosts)

terraform init $TERRAFORM_CONFIG
terraform apply -state=$TERRAFORM_STATE -auto-approve $TERRAFORM_CONFIG
terraform providers $TERRAFORM_CONFIG
terraform output -state=$TERRAFORM_STATE instance_names
terraform output -state=$TERRAFORM_STATE -json instance_ips | jq --raw-output '.[]' > $NODES_FILENAME
pushd $TESTS_DIR && lein run test $LEIN_OPTIONS
popd
cleanup
