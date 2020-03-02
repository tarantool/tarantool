#!/usr/bin/env python3

# This script uploads to remote server packages,
# created by packpack  and stored in "/build".
# directory, to remote server. It uses paramiko
# library to establish client sftp connection.
# It is aware of stucture of package repository,
# located at https://download.picodata.io. It is
# actually part of internal package distribution
# system, and has nothing to do with Tarantool.
# However, it is kept here for ease of operations.

import os
import subprocess
import requests

import paramiko

# Executes shell command and returns its output.
def exec_capture_stdout(cmd):
    proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
    (stdout, stderr) = proc.communicate()
    return stdout.strip().decode('utf-8')


git_version_cmd = 'git describe --long --always'

# Returns major version like 1.10, 2.2, 3.5, etc.
def get_version():
    version = exec_capture_stdout(git_version_cmd)
    version_array = version.split('-')
    version_1 = version_array[0]
    version_array_1 = version_1.split('.')
    version_2 = '.'.join([version_array_1[0], version_array_1[1]])
    return version_2


def is_deb(os):
    return (os == 'debian' or os == 'ubuntu')


def is_rpm(os):
    return (os == 'el' or os == 'fedora')

# Uploads all files from local directory to specified
# remote directory via SFTP.
def upload_deb(sftp, src_dir, dst_dir):
    for f in os.listdir(src_dir):
        src_file = os.path.join(src_dir, f)
        dst_file = os.path.join(dst_dir, f)
        print('uploading %s to %s' % (src_file, dst_file))
        sftp.put(src_file, dst_file)

    update_tarantool_repo(dst_dir)

# Uploads .rpm and .src.rpm files from local directory
# to specified remote directory via SFTP.
def upload_rpm(sftp, src_dir, dst_dir):
    dist_dir_x86_64 = '/'.join([dst_dir, 'x86_64'])
    dist_dir_sprms = '/'.join([dst_dir, 'SRPMS'])

    for f in os.listdir(src_dir):
        src_file = os.path.join(src_dir, f)
        dst_file = ''

        if f.endswith('.src.rpm'):
            dst_file = os.path.join(dist_dir_sprms, f)
        elif f.endswith('.rpm'):
            dst_file = os.path.join(dist_dir_x86_64, f)

        if dst_file != '':
            print('uploading %s to %s' % (src_file, dst_file))
            sftp.put(src_file, dst_file)

    update_tarantool_repo(dist_dir_x86_64)
    update_tarantool_repo(dist_dir_sprms)

# Notifies repository manager (internal system) to update
# index metadata and signature of repository at sepcifed
def update_tarantool_repo(repo_path):
    env_repo_manager_host = os.environ.get("REPO_MANAGER_HOST")
    env_repo_manager_port = int(os.environ.get("REPO_MANAGER_PORT"))

    addr = 'http://%s:%s/updateRepo' % (env_repo_manager_host,
                                        env_repo_manager_port)
    repo_path = '/'.join(['tarantool', repo_path])

    print("updating index and signature of %s" % (repo_path))

    requests.post(addr, json={"path": repo_path})

    return


def main():
    # Get configuration from Travis variables.
    env_sftp_host = os.environ.get('SFTP_HOST')
    env_sftp_port = int(os.environ.get('SFTP_PORT'))

    env_sftp_user = os.environ.get('SFTP_USER')
    env_sftp_pass = os.environ.get('SFTP_PASSWORD')

    env_os = os.environ.get('OS')
    env_dist = os.environ.get('DIST')
    version = get_version()

    # Establish SFTP connection.
    t = paramiko.Transport((env_sftp_host, env_sftp_port))
    t.connect(None, env_sftp_user, env_sftp_pass)
    sftp = paramiko.SFTPClient.from_transport(t)

    # Determine directory of package repository and its type.
    src_dir = 'build/'
    dst_dir = '/'.join([version, env_os, env_dist])

    if is_deb(env_os):
        upload_deb(sftp, src_dir, dst_dir)
    elif is_rpm(env_os):
        upload_rpm(sftp, src_dir, dst_dir)
    else:
        print("unknown package type")
        return


if __name__ == "__main__":
    main()
