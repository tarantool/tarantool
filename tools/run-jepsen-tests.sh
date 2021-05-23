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
# and two arguments: path to a Tarantool project source directory and path to a
# Tarantool project binary directory. And optional use environment variables:
# LEIN_OPT and INSTANCE_COUNT.

set -Eeo pipefail

function usage {
    echo "Usage: $0 <PROJECT_SOURCE_DIR> <PROJECT_BINARY_DIR>"
    echo "Options:"
    echo "  PROJECT_SOURCE_DIR - path with project sources"
    echo "  PROJECT_BINARY_DIR - path to build the project"
    echo "Mandatory environment:"
    echo "  TF_VAR_ssh_key - SSH private key to reach the testing nodes"
    echo "  TF_VAR_keypair_name - key pair name used by Terraform"
    echo "  TF_VAR_user_name - user name used by Terraform to reach the testing nodes"
    echo "  TF_VAR_password - password used by Terraform"
    echo "  TF_VAR_tenant_id - tenant ID used by Terraform"
    echo "  TF_VAR_user_domain_id - users domain ID used by Terraform"
    echo "Optional environment:"
    echo "  LEIN_OPT - Jepsen tests additional options, ex. '--nemesis standard', default is empty"
    echo "  INSTANCE_COUNT - number of nodes to be tested, default is '1'"
    exit 1
}

################################################
# check script startup environment and options #
################################################

# get mandatory options from script run command
PROJECT_SOURCE_DIR=$1
PROJECT_BINARY_DIR=$2
[[ -z ${PROJECT_SOURCE_DIR} ]] && (echo "Please specify path to a project source directory"; usage)
[[ -z ${PROJECT_BINARY_DIR} ]] && (echo "Please specify path to a project binary directory"; usage)

# check if mandatory variables were set in environment
[[ -z ${TF_VAR_ssh_key} ]] && (echo "Please specify TF_VAR_ssh_key env var"; usage)
[[ -z ${TF_VAR_keypair_name} ]] && (echo "Please specify TF_VAR_keypair_name env var"; usage)

# check if mandatory variables were provided even by CI
[[ -z ${TF_VAR_user_name} ]] && (echo "Please specify TF_VAR_user_name env var"; usage)
[[ -z ${TF_VAR_password} ]] && (echo "Please specify TF_VAR_password env var"; usage)
[[ -z ${TF_VAR_tenant_id} ]] && (echo "Please specify TF_VAR_tenant_id env var"; usage)
[[ -z ${TF_VAR_user_domain_id} ]] && (echo "Please specify TF_VAR_user_domain_id env var"; usage)

# check existance of tools and setup its paths
TERRAFORM_BIN=$(which terraform)
LEIN_BIN=$(which lein)
CLOJURE_BIN=$(which clojure)

[[ -z ${TERRAFORM_BIN} ]] && (echo "terraform is not installed"; exit 1)
[[ -z ${LEIN_BIN} ]] && (echo "lein is not installed"; exit 1)
[[ -z ${CLOJURE_BIN} ]] && (echo "clojure is not installed"; exit 1)

################################
# make script run preparations #
################################

# setup files paths
BUILD_DIR="$PROJECT_SOURCE_DIR/build"
TESTS_DIR="$PROJECT_BINARY_DIR/jepsen-tests-prefix/src/jepsen-tests/"
TERRAFORM_CONFIG="$PROJECT_SOURCE_DIR/extra/tf"
TERRAFORM_STATE="$PROJECT_BINARY_DIR/terraform.tfstate"
CUR_DIR=$(pwd)
SSH_KEY_FILENAME="$HOME/.ssh/id_rsa"
NODES_FILENAME="$BUILD_DIR/nodes"

# build directory should be prepared before use
[[ -d $BUILD_DIR ]] || mkdir -p $BUILD_DIR

# git must initialized before use even to any unknown user
CI_COMMIT_SHA=$(git rev-parse HEAD)

# setup cleanup routine, which removes testing nodes by Terraform
function cleanup {
    echo "Cleanup running ..."
    cd $CUR_DIR
    rm -f $NODES_FILENAME $SSH_KEY_FILENAME
    [[ -e $TERRAFORM_STATE ]] && \
	terraform destroy -state=$TERRAFORM_STATE -auto-approve $TERRAFORM_CONFIG
}

# set signal handler on script interrupts
trap "{ cleanup; exit 255; }" SIGINT SIGTERM ERR

# setup SSH home path with needed files
[[ -d $HOME/.ssh ]] || mkdir -p $HOME/.ssh
chmod 700 $HOME/.ssh

# remove extra spaces from TF_VAR_ssh_key key at the end of lines
SSH_KEY=$(echo -e "$TF_VAR_ssh_key" | sed 's# *$##g')

# Create file with SSH private key, add it to SSH agent and
# setup Terraform mandatory TF_VAR_ssh_key_path with its name.
echo -e "${SSH_KEY//_/\\n}" >$SSH_KEY_FILENAME
chmod 600 $SSH_KEY_FILENAME
# reinitialize SSH agent
eval "$(ssh-agent -s)"
ssh-add $SSH_KEY_FILENAME
# check that SSH key added
ssh-add -l
export TF_VAR_ssh_key_path=$SSH_KEY_FILENAME
export TF_VAR_ssh_key=$(cat $SSH_KEY_FILENAME)

# setup Terraform mandatory TF_VAR_id variable to run the nodes
if [[ -n ${CI_JOB_ID} ]]; then
    TF_VAR_id=$CI_JOB_ID
else
    RANDOM_ID=$(head /dev/urandom | tr -dc A-Za-z0-9 | head -c 13; echo '')
    TF_VAR_id=TF-$RANDOM_ID
fi
export TF_VAR_id

# setup Terraform mandatory TF_VAR_instance_count with nodes number
[[ -z ${INSTANCE_COUNT} ]] && TF_VAR_instance_count=1 || TF_VAR_instance_count=$INSTANCE_COUNT
export TF_VAR_instance_count

###########################
# main part of the script #
###########################

# 1. initiate Terraform configuration for node setup
terraform init $TERRAFORM_CONFIG

# 2. setup node and create its configuration in $TERRAFORM_STATE file
terraform apply -state=$TERRAFORM_STATE -auto-approve $TERRAFORM_CONFIG

# 3. print the schemas of the providers used in the configuration
terraform providers $TERRAFORM_CONFIG

# 4. read an output variable from a Terraform state file and print the value
terraform output -state=$TERRAFORM_STATE instance_names

# 5. get IP of the node from Terraform state file
terraform output -state=$TERRAFORM_STATE -json instance_ips | jq --raw-output '.[]' > $NODES_FILENAME

# 6. add nodes to SSH known hosts and check that they are reachable by SSH
for node in $(cat $NODES_FILENAME) ; do
    ssh -vvv -o "StrictHostKeyChecking=no" -o "BatchMode=yes" -i $SSH_KEY_FILENAME ubuntu@$node hostname
done

# 7. run Jepsen tests
( cd $TESTS_DIR && lein run test-all --nodes-file $NODES_FILENAME --username ubuntu $LEIN_OPT --version $CI_COMMIT_SHA )

# post script cleanup
cleanup
