#!/bin/bash
rm -f *.rpm
chmod 755 install.ksh
sudo yum -y remove "ffmpeg*" x264
sudo yum -y install ffmpeg-0.4.1 ffmpeg-devel-0.4.1 asteriskv-devel mpeg4ip-devel
sudo yum -y install asteriskv
sudo yum -y install SDL-devel
./install.ksh rpm nosign