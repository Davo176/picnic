#!/bin/bash
cd Reference_Implementation
allFolders=$(ls)

for folder in $allFolders
do
    cd $folder
    echo $folder
    cd NIST-KATs
    cp ../../picnic3l1/NIST-KATs/checkSigned.c checkSigned.c
    cd ..
    cd ..
done