#!/bin/bash
cd Reference_Implementation
allFolders=$(ls)

for folder in $allFolders
do
    cd $folder
    echo $folder
    make createkp
    ./NIST-KATs/createKeyPairs
    pwd
    cd ..
done