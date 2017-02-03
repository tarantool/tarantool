#!groovy

stage('Preparation') {
    node {
        checkout scm
        dir("build") {
            git 'https://github.com/tarantool/build.git'
        }
        stash name: 'source', useDefaultExcludes: false
    }
}


stage('Build') {
    timeout(time: 2, unit: 'HOURS') {
        parallel (
            "el6": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="el"; env.DIST="6"; env.PACK="rpm"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "el7": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="el"; env.DIST="7"; env.PACK="rpm"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "fedora23": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="fedora"; env.DIST="23"; env.PACK="rpm"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "fedora24": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="fedora"; env.DIST="24"; env.PACK="rpm"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "fedora25": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="fedora"; env.DIST="25"; env.PACK="rpm"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "fedorarawhide": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="fedora"; env.DIST="rawhide"; env.PACK="rpm"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "ubuntuprecise": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="ubuntu"; env.DIST="precise"; env.PACK="deb"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "ubuntutrusty": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="ubuntu"; env.DIST="trusty"; env.PACK="deb"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "ubuntuxenial": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="ubuntu"; env.DIST="xenial"; env.PACK="deb"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "ubuntuyakkety": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="ubuntu"; env.DIST="yakkety"; env.PACK="deb"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            /*"debianwheezy": {
              node {
              ws {
              deleteDir()
              unstash 'source'
              env.OS="debian"; env.DIST="wheezy"; env.PACK="deb"
              sh './build/pack/travis.sh'
              }
              }
              },*/
            "debianjessie": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="debian"; env.DIST="jessie"; env.PACK="deb"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            "debianstretch": {
                node {
                    ws {
                        deleteDir()
                        unstash 'source'
                        env.OS="debian"; env.DIST="stretch"; env.PACK="deb"
                        sh './build/pack/travis.sh'
                    }
                }
            },
            /*"debiansid": {
              node {
              ws {
              deleteDir()
              unstash 'source'
              env.OS="debian"; env.DIST="sid"; env.PACK="deb"
              sh './build/pack/travis.sh'
              }
              }
              }*/
        )
    }
}
