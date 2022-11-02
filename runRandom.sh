#!/bin/bash
cd Reference_Implementation
allFolders=$(ls)

for folder in $allFolders
do
    cd $folder
    echo $folder
    make all
    make nistkat
    ./NIST-KATs/addRand
    pwd
    cd ..
done