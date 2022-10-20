#!/bin/bash
cd Reference_Implementation
allFolders=$(ls)
i=0
for folder in $allFolders
do
    if [ $i -gt 7 ]
    then
        cd $folder
        echo $folder
        make checksign
        echo $folder
        ./NIST-KATs/checkSigned
        cd ..
    fi
    i=$(($i+1))
    
done