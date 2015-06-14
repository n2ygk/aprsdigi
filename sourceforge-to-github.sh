#!/bin/bash
# one time script to move aprsdigi from sourceforge CVS to github
# meant to be run with vagrant.
# convert CVS to git: http://cvs2svn.tigris.org/cvs2git.html
echo installing cvs2git
curl -o cvs2svn.tgz http://cvs2svn.tigris.org/files/documents/1462/49237/cvs2svn-2.4.0.tar.gz
tar xfz cvs2svn.tgz
cd cvs2svn-2.4.0
sudo make install
cd
echo copying aprsdigi CVS repo from sourceforge
rsync -av 'rsync://n2ygk@aprsdigi.cvs.sourceforge.net/cvsroot/aprsdigi/*' .
mv aprsdigi sf-aprsdigi
echo converting CVS to git
mkdir cvs2git-tmp
cvs2git --blobfile=cvs2git-tmp/git-blob.dat \
	--dumpfile=cvs2git-tmp/git-dump.dat \
	--username=cvs2git \
	aprsdigi
# get my git configuration
cp /vagrant/.gitconfig ~
git init --bare aprsdigi.git
cd aprsdigi.git
cat ../cvs2git-tmp/git-blob.dat ../cvs2git-tmp/git-dump.dat | git fast-import
git branch -D TAG.FIXUP
git gc --prune=now
cd
git clone aprsdigi.git
cd aprsdigi

# get rid of aprsdigi.git as the 'origin' remote
git remote remove origin
# set up aprsdigi on github
git remote add origin git@github.com:n2ygk/aprsdigi.git
git push -u origin master
mv aprsdigi /vagrant # so I can edit in my "normal" environment




