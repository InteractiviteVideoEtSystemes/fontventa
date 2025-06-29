Name:           fontventa
Version:        1.6.17
Release:        1.ives%{?dist}
Summary:        Fontventa shared libraries for Asterisk
License:        GPL
URL:            http://sip.fontventa.com/
BuildRequires:  asteriskv-devel
BuildRequires:  SDL-devel
BuildRequires:  gcc-c++
BuildRequires:  ffmpeg-devel
BuildRequires:  x264-devel
BuildRequires:  mp4v2-devel
Requires:       asteriskv
Requires:       bc
Requires:       mp4v2
Requires:       ffmpeg

%description
Fontventa shared libraries for Asterisk.
Add h324m support.

%build
%{make_build}

%install
%{make_install} DESTDIR=$RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_libdir}/asterisk/modules/app_*.so
#/usr/include/*.h
/usr/bin/IVES_convert.ksh
/usr/bin/mp4asterisk
/usr/bin/mp4band
/usr/bin/mp4creator
/usr/bin/mp4tool
/usr/bin/pcm2mp4
/usr/sbin/astlog
#%config(noreplace) %attr(0640,root,root) /etc/asterisk/*.conf

%changelog
* Fri May 22 2020 Emmanuel BUU <emmanuel.buu@ives.fr>
- memory leak for mp4play() in app_mp4
- version 1.6.13

* Mon May 11 2020 Emmanuel BUU <emmanuel.buu@ives.fr>
- minor memory leak for mp4save() in app_mp4
- memory leak for mp4play() in app_mp4
- version 1.6.12

* Wed May 15 2019 Emmanuel BUU <emmanuel.buu@ives.fr>
- mp4save() now removes the MP4 file automatically if video has not started.
- this is the correction of bug SC-57
- corrected no audio on MP4play when using Voximal
- see libedikit log for details
- version 1.6.7

* Tue Apr 23 2019 Emmanuel BUU
- corrected regression on MP4play when using Voximal
- see limedikit log for details
- version 1.6.6

* Thu Apr 11 2019  Emmanuel BUU
- corrected regression on audio/video sync
- version 1.6.5

* Tue Mar 26 2019 Emmanuel BUU
- backported improvment of IVES_convert script from branch 0.5
- version 1.6.4

* Tue Mar 12 2019 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected RTT recorind in text file
- integrated with VM
- see libmedkit logs for details
- version 1.6.3

* Mon Jul 16 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected SPS decoding in libmedikit
- version 1.6.2

* Thu Jun 7 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected tools to use mp4v2
- version 1.6.1

* Wed Jun 6 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- tested recording and play correctly
- migrated to ffmpeg 3.3.7
- version 1.6.0
* Fri Mar 16 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected recorder and player. mp4record and play are now using libmedkit
- version 1.4.0
* Thu Feb 22 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- reimplemented mp4play / mp4save using libmedikit
- version 1.0.0

