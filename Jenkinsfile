pipeline {
    agent any 
    stages {
        stage('build') {
            steps {
                sh 'docker/dockerbuild.sh'
            }
        }
    }
    post {
        always {
            archiveArtifacts artifacts: '*.deb'
        }
    }
}
