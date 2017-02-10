#!groovy

stage('Build'){
    packpack = new org.tarantool.packpack()
    node {
        checkout scm
        packpack.prepareSources()
    }

    packpack.packpackBuildMatrix('result')
}
