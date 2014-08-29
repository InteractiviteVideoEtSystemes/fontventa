#!/bin/bash
rm -f *.rpm
chmod 755 install.ksh
sudo yum -y remove "ffmpeg*" "x264*" "mpeg4ip-devel*" "amrnb*" "gsm*" "lame*" "mpeg4ip*" "asteriskv*" "SDL*"
sudo yum -y install ffmpeg-0.4.2 ffmpeg-devel-0.4.2 asteriskv-devel mpeg4ip-devel-1.5.2  "asteriskv" "SDL-devel x264-devel*"
./install.ksh rpm nosign
