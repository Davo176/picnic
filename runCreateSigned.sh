#!/bin/bash
cd Reference_Implementation
allFolders=$(ls)

for folder in $allFolders
do
    cd $folder
    echo $folder
    make all
    make createsign
    ./NIST-KATs/createSigned
    echo $folder
    cd ..
done