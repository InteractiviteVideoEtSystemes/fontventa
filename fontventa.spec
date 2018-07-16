Name:      fontventa
Version:   1.6.0
#Ne pas enlever le .ives a la fin de la release !
#Cela est utilise par les scripts de recherche de package.
Release:   1.ives%{?dist}
Summary:   [IVeS] librairies partag�es pour asterisk de Fontventa.
Vendor:   IVeS
Group:     Applications/Internet
License: GPL
URL:       http://www.ives.fr
BuildArchitectures: x86_64 i686 i386 i586
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires:  ivespkg,  asteriskv , bc, mpeg4ip >= 2.0.0, ffmpeg >= 3.3.0   
BuildRequires: asteriskv-devel, SDL-devel, gcc-c++   

%description
Un ensemble de librairies partag�es pour asterisk de Fontventa.
  
%clean
echo "############################# Clean"
echo Clean du repertoire $RPM_BUILD_ROOT
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf "$RPM_BUILD_ROOT"

%prep
echo "Export du SVN ives"
svn export --force http://svn.ives.fr/svn-libs-dev/asterisk/%name/tags/%version

%build
echo "Build"
cd %version
#Repertoire d'installation des librairies
if [ "`uname -m`" == "x86_64" ]
then
        DESTDIR_LIB=/usr/lib64
else
        DESTDIR_LIB=/usr/lib
fi
echo "SYS_LIB=$DESTDIR_LIB" >Makeinclude
#
make

%install
echo "############################# Install"
cd %version
make DESTDIR=$RPM_BUILD_ROOT install

%files
%defattr(-,root,root,-)
%{_libdir}/
#/usr/include/*.h
/usr/bin/*
/usr/sbin/astlog
#%config(noreplace) %attr(0640,root,root) /etc/asterisk/*.conf

%changelog
* Mon Jul 16 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected SPS decoding in libmedikit
- version 1.6.2

* Wed Jun 7 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected tools to use mp4v2
- version 1.6.1

* Wed Jun 6 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- tested recording and play correctly
- migrated to ffmpeg 3.3.7
- version 1.6.0
* Mon Mar 16 2018 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected recorder and player. mp4record and play are now using libmedkit
- version 1.4.0
* Thu Feb 22 2018 Emmanuel BUU  Emmanuel BUU <emmanuel.buu@ives.fr>
- reimplemented mp4play / mp4save using libmedikit
- version 1.0.0
* Fri Nov 21 2014 Emmanuel BUU <emmanuel.buu@ives.fr>
- accent were filtered. Removed filter.
- version 0.5.8
* Tue Aug 26 2014 Philippe Verney <philippe.verney@ives.fr>
- Suppress transcoder ( link error with ffmpeg ) 
- Fix video echo on H.264
- version 0.5.7
* Wed Dec 5 2013 Philippe Verney <philippe.verney@ives.fr>
- Fix pb de synchro audio / video h264
- version 0.5.6
* Mon Jul 15 2013 Maquin Olivier <olivier.maquin@ives.fr>
- static link with FFMPEG , suppress x264 , static with mp4IP
- version 0.5.4
* Wed May 22 2013 Emmanuel BUU <emmannuel.buu@ives.fr>
- static link with FFMPEG and X264
- version 0.5.3
* Fri Nov 09 2012 Philippe Verney <philippe.verney@ives.fr> 
- version 0.5.2
- Add filter BOM on T140
- Change stat string to resolv conflit "video" find on mp4
* Tue Jun 21 2012 Philippe Verney <philippe.verney@ives.fr> 
- version 0.5.1
- Add rtp stat on file   
* Tue Jun 20 2012 Philippe Verney <philippe.verney@ives.fr> 
- version 0.5.0
- Fix h263 play/rec , fix T140 record msg
* Fri Jun 01 2012 Thomas Carvello <thomas.carvello@ives.fr> 
- version 0.4.1
* Tue May 15 2012 Olivier Maquin <olivier.maquin@ives.fr> 
- version 0.4.0
- static linking for libpt
- compatibility of app_transcoder with ffmpeg 0.9.1
* Wed Jul 27 2011 Emmanuel BUU <emmanuel.buu@ives.fr>
- support for multiple audo codec negocation (asterisk-i1.4.19r)
* Wed Jul 7 2011 Sergio <sergio@fontventa.com>
- added H.264 support for Transcoder application
* Wed Sep 24 2010 Philippe Verney <philippe.verney@ives.fr>
- Suppress work arround on h323
* Mon Mar 17 2010 Philippe Verney <philippe.verney@ives.fr>
- new well tested fix for video timestamping
* Wed Mar 17 2010 Philippe Verney <philippe.verney@ives.fr>
- fixed issue with video packet in bad order when recording
* Mon Jan 18 2010 Philippe Verney <philippe.verney@ives.fr>
- Fix IVES_convert.ksh ( No sound track & video quality )
* Fri Aug 7 2009 Emmanuel BUU <emmanuel.buu@ives.fr>
- Added IVES_convert.ksh script, added compilation requirement in .spec
- added ability to configure H.245 bit order in h324M.conf
* Sun Jun 14 2009 Emmanuel BUU <emmanuel.buu@ives.fr>
- 0.2.1 -> support enreg test. Suppression logs inutiles
* Fri Apr 17 2009 Eric Delas <eric.delas@ives.fr> 0.1.1-3.ives
- Package for 1.4.19f-1.ives asterisk version
* Mon Apr 07 2009 Emmanuel BUU <emmanuel.buu@ives.fr> 0.1.1-2.ives
- Integration livraison app_mp4 de Borja
* Mon Mar 09 2009 Didier Chabanol <didier.chabanol@ives.fr> 0.1.0-1.ives
- Initial package

