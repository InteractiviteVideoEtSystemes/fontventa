#!/bin/bash
rm -f *.rpm
chmod 755 install.ksh
sudo yum -y remove x264 ffmpeg
sudo yum -y install ffmpeg-devel-0.4.1 asteriskv-devel mpeg4ip-devel
sudo yum -y install asteriskv
sudo yum -y install SDL-devel
./install.ksh rpm nosign