pipeline {
    agent any 
    stages {
        stage('build') {
            steps {
                sh docker/dockerbuild.sh
            }
        }
    }
}
