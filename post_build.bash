#!/bin/bash
sudo yum -y remove fontventa
sudo yum localinstall --nogpgcheck -y fontventa*.rpm
/usr/bin/IVeS_ffmpeg -i PUB_Guerlain_-_La_petite_robe_noire.mp4 -o test_convert.mp4
ENVOYER_VISIO2=$1
if [ "$ENVOYER_VISIO2" != "" ] 
then
    scp -oPort=2222 *.rpm jenkins@visio2.visioassistance.net:/home/jenkins/$NODE_NAME
else
    echo "On n'envoie rien sur visio2"
fi
