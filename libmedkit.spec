Name:      libmedkit
Version:   1.6.17
#Ne pas enlever le .ives a la fin de la release !
#Cela est utilise par les scripts de recherche de package.
Release:   1.ives%{?dist}
Summary:   [IVeS] librairies multemedia pour app IVes
Vendor:   IVeS
Group:     Applications/Internet
License: GPL
URL:       http://www.ives.fr
BuildArchitectures: x86_64
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires:  ivespkg 
BuildRequires: ffmpeg-devel >= 1.3, gcc-c++, asteriskv-devel, x264-devel, mpeg4ip-devel

%description
Un ensemble de librairies partag�es pour asterisk de Fontventa.
  
%clean
echo "############################# Clean"
cd $RPM_SOURCE_DIR/%name
make clean
cd ..
rm -f %name
echo Clean du repertoire $RPM_BUILD_ROOT
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf "$RPM_BUILD_ROOT"

%prep

%build
echo "Build"
cd $RPM_SOURCE_DIR/%name
make all

%install
echo "############################# Install"
cd $RPM_SOURCE_DIR/%name
cd libmedikit
make DESTDIR=$RPM_BUILD_ROOT install

%files
%defattr(-,root,root,-)
/opt/ives/%{_lib}/
/opt/ives/include/astmedkit/
/opt/ives/include/medkit/

%changelog
* Thu May 05 2021 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected handling of CFLF in text recording
- corrected handling of large redundent text packets
- version 1.6.14

* Fri May 22 2020 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected memory leaks in mp4track and mp4format
- simplified packaging.
- version 1.6.12

* Thu May 15 2019 Emmanuel BUU <emmanuel.buu@ives.fr>
- this is the correction of bug SC-57
- corrected no audio on MP4play when using Voximal
- version 1.6.7

* Thu Apr 11 2019  Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected regression on audio/video sync
- version 1.6.5

* Mon Jul 16 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected SPS decoding
- version 1.6.2

* Wed Jun 6 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- migrated to ffmpeg 3.3.7
- version 1.6.0

* Wed Mar 19 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected error in function signature on AstFbWaitMulti() on framebuffer.h
- version 1.5.1

* Wed Mar 19 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- recoded jitterbuffer to remove pipe because it was exhausting the max number of open file desc.
- removed C++ 11 mutex.lock() to avoid building issues
- version 1.5.0

* Mon Mar 16 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected recorder and player. mp4record and play are now using libmedkit
- version 1.4.0

* Mon Feb 5 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- buffer overflow correction in traces
- version 1.3.1

* Fri Dec 21 2017 Emmanuel BUU <emmanuel.buu@ives.fr>
- major rework on recorder
- jitterbuffer sync based on pipe rather than conditional variable
- use C++ 11 primitives
- version 1.3.0

* Fri Oct 28 2016 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected a locking issue in jitter buffer
- version 1.2.2

* Thu Apr 21 2016 Emmanuel BUU <emmanuel.buu@ives.fr>
- finished packaging
- updated dependencies to ffmpeg 2.4.x
- version 1.2.1

* Wed Jul 7 2015 Emmanuel BUU <emmanuel.buu@ives.fr>
- ajout lecture fichier MP4 dans mp4format.cpp
- version 1.2.0

* Wed Jul 7 2015 Emmanuel BUU <emmanuel.buu@ives.fr>
- ajout classe PictureStreamer
- separation mp4format en deux fichiers
- version 1.1.0

* Wed May 7 2014 Emmanuel BUU <emmanuel.buu@ives.fr>
- packaging of libmedkit
