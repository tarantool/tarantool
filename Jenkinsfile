#!groovy

stage('Build') {
    node {
        checkout scm
        p = new org.tarantool.packpack()
        p.packpackBuildMatrix('result')
    }
}
