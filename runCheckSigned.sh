#!/bin/bash
cd Reference_Implementation
allFolders=$(ls)

for folder in $allFolders
do
    cd $folder
    echo $folder
    make all
    make checksign
    echo $folder
    ./NIST-KATs/checkSigned
    cd ..
    
done