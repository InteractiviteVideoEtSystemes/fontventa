#!/bin/bash

PROJET=fontventa
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

function svn_export
{
        svn export http://svn.ives.fr/svn-libs-dev/asterisk/${PROJET}
}


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
    if [ -r gnupg/ ] ; then
        echo -e "${LINE} [ ${YELLOW}ALREADY EXISTS${NC} ]\r  $FLECHE Import de la clef GPG IVeS gnupg/"
    else
        git clone git@git.ives.fr:internal/gnupg.git
        if [ -r gnupg/ ] ; then
	    rm -rf gnupg/.git
            echo -e "${LINE} [ ${GREEN}OK${NC} \xE2\x9C\x94 ]\r  $FLECHE Import de la clef GPG IVeS"
        else
            echo -e "${LINE} [ ${RED}ERROR${NC} \xe2\x9c\x97 ]\r  $FLECHE Import de la clef GPG IVeS"
        fi
    fi
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
        mv -f $PWD/rpmbuild/RPMS/x86_64/*.rpm $PWD/.
    fi
    clean
}

function clean
{
        # On efface les liens ainsi que le package precedemment crÃÃs
        echo Effacement des fichiers et liens
        rm -f rpmbuild/SOURCES/${PROJET}
        rm -f rpmbuild/SPECS/${PROJET}.spec
	rm -rf rpmbuild gnupg
}

case $1 in
  	"clean")
  		echo "Nettoyage des liens et du package crees par la cible dev"
  		clean ;;
  	"rpm")
  		echo "Creation du rpm"
  		create_rpm $2;;

	"prereq")
		sudo yum -y install ffmpeg-devel mpeg4ip-devel asteriskv-devel SDL-devel x264-devel ;;
  	*)
  		echo "usage: install.ksh [options]" 
  		echo "options :"
  		echo "  rpm     Generation d'un package rpm"
  		echo "  prereq  Install des prerequis"
  		echo "  clean   Nettoie tous les fichiers ";;
esac
