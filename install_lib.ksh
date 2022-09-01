#!/bin/bash

PROJET=libmedkit
#Repertoire d'installation des includes
DESTDIR_INC=/usr/include/
#Repertoire d'installation des librairies
if [ "`uname -m`" == "x86_64" ]
then
	DESTDIR_LIB=/usr/lib64
else
	DESTDIR_LIB=/usr/lib
fi
echo "SYS_LIB=$DESTDIR_LIB" >Makeinclude

#RPepertoire d'installation des fichiers so
DESTDIR_MOD=$DESTDIR_LIB/asterisk/modules
#Repertoire d'installation du fichier mp4tool
DESTDIR_BIN=/usr/bin/
#Repertoire temporaire utiliser pour preparer les packages
TEMPDIR=/tmp

# Le premier parametre donne le repertoire destination

#Creation de l'environnement de packaging rpm
function create_rpm
{
    #Cree l'environnement de creation de package
    #Creation des macros rpmbuild
    rm ~/.rpmmacros
    touch ~/.rpmmacros
    echo "%_topdir" $PWD"/rpmbuild" >> ~/.rpmmacros
    echo "%_tmppath %{_topdir}/TMP" >> ~/.rpmmacros
    echo "%_signature gpg" >> ~/.rpmmacros
    echo "%_gpg_name IVeSkey" >> ~/.rpmmacros
    echo "%_gpg_path" $PWD"/gnupg" >> ~/.rpmmacros
    echo "%vendor IVeS" >> ~/.rpmmacros
    #Import de la clef gpg IVeS
    svn export https://svn.ives.fr/svn-libs-dev/gnupg
    mkdir -p rpmbuild
    mkdir -p rpmbuild/SOURCES
    mkdir -p rpmbuild/SPECS
    mkdir -p rpmbuild/BUILD
    mkdir -p rpmbuild/SRPMS
    mkdir -p rpmbuild/TMP
    mkdir -p rpmbuild/RPMS
    mkdir -p rpmbuild/RPMS/noarch
    mkdir -p rpmbuild/RPMS/i386
    mkdir -p rpmbuild/RPMS/i686
    mkdir -p rpmbuild/RPMS/i586
    mkdir -p rpmbuild/RPMS/x86_64
    #Recuperation de la description du package 
    cd ./rpmbuild/SPECS/
    cp ../../${PROJET}.spec ${PROJET}.spec
    cd ../SOURCES
    ln -s ../.. ${PROJET}
    cd ../../
    #Cree le package
    if [[ -z $1 || $1 -ne nosign ]]
    then
             rpmbuild -bb --sign $PWD/rpmbuild/SPECS/${PROJET}.spec
    else
             rpmbuild -bb $PWD/rpmbuild/SPECS/${PROJET}.spec
    fi

    if [ $? == 0 ]
    then
        echo "************************* fin du rpmbuild ****************************"
        #Recuperation du rpm
        mv -f $PWD/rpmbuild/RPMS/i386/*.rpm $PWD/.
        mv -f $PWD/rpmbuild/RPMS/x86_64/*.rpm $PWD/.
    fi
    clean
}

function clean
{
  	# On efface les liens ainsi que le package precedemment cr��
  	echo Effacement des fichiers et liens gnupg rpmbuild ${PROJET}.rpm ${TEMPDIR}/${PROJET}
  	rm -rf gnupg rpmbuild ${PROJET}.rpm ${TEMPDIR}/${PROJET}
}

case $1 in
  	"clean")
  		echo "Nettoyage des liens et du package crees par la cible dev"
  		clean ;;
	"prereq")
                sudo yum -y install ffmpeg-devel mpeg4ip-devel asteriskv-devel SDL-devel x264-devel ;;

  	"rpm")
  		echo "Creation du rpm"
  		create_rpm $2;;
  	*)
  		echo "usage: install_lib.ksh [options]" 
  		echo "options :"
  		echo "  rpm 	Generation d'un package rpm"
  		echo "  prereq	Installation des prerequis"
  		echo "  clean	Nettoie tous les fichiers cree par le present script, liens, tar.gz et rpm";;
esac
