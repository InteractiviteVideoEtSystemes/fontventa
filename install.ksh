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


#Copie des fichiers composants le rpm dans un repertoire temporaire
# Le premier parametre donne le repertoire destination
function copy_rpmInstall
{
    if [ ! -d $1 ]
      then 
        echo "[ERROR] Veuillez passer en parametre a install.ksh le repertoire temporaire de destination"
        return
    fi
    if [ ! -d ./$PROJET ]
      then
        echo "[ERROR] Repertoire projet inexistant. Verifier le checkout dans " $PWD
        return
    fi
  
    #Copie des shared lib
    mkdir -p $1/$DESTDIR_INC
    mkdir -p $1/$DESTDIR_LIB
    mkdir -p $1/$DESTDIR_MOD
    mkdir -p $1/$DESTDIR_BIN
    pwd
    cp $PROJET/app_mp4/app_mp4.so $1/$DESTDIR_MOD/app_mp4.so 
    chmod 755 $1/$DESTDIR_MOD/app_mp4.so
    #cp $PROJET/app_transcoder/app_transcoder.so $1/$DESTDIR_MOD/app_transcoder.so 
    #chmod 755 $1/$DESTDIR_MOD/app_transcoder.so
    cp $PROJET/app_rtsp/app_rtsp.so $1/$DESTDIR_MOD/app_rtsp.so 
    chmod 755 $1/$DESTDIR_MOD/app_rtsp.so
    cp $PROJET/tools/mp4tool $1/$DESTDIR_BIN/mp4tool
	cp $PROJET/tools/pcm2mp4 $1/$DESTDIR_BIN/pcm2mp4
    cp $PROJET/tools/IVES_convert.ksh $1/$DESTDIR_BIN/IVES_convert.ksh
    cp /usr/bin/ffmpeg $1/$DESTDIR_BIN/IVeS_ffmpeg
    chmod 755 $1/$DESTDIR_BIN/mp4tool
	chmod 755 $1/$DESTDIR_BIN/pcm2mp4
    chmod 755 $1/$DESTDIR_BIN/IVES_convert.ksh
    chmod 755 $1/$DESTDIR_BIN/IVeS_ffmpeg
    cp Makeinclude $1/
    echo "Fin de la copie des fichiers dans " $1
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
  	"rpmInstall")
  		#rpmInstall est appele automatiquement par le script de creation de rpm
  		echo "Copie des fichiers du rpm dans la localisation temporaire"
  		copy_rpmInstall $2;;
  	"rpm")
  		echo "Creation du rpm"
  		create_rpm $2;;
  	*)
  		echo "usage: install.ksh [options]" 
  		echo "options :"
  		echo "  rpm		Generation d'un package rpm"
  		echo "  clean		Nettoie tous les fichiers cree par le present script, liens, tar.gz et rpm";;
esac
