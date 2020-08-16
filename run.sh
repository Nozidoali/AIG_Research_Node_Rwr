make 
cd ..
echo "file,oldsize,oldlevel,rwrsize,rwrlevel," > test.csv
for file in benchmarks/random_control/*.blif
do
    if test -f $file ;
    then
        name=`basename $file`
        filename="${name%%.*}"
        echo ${filename} 
        echo "${filename},\c " >> test.csv
        ./why/main ${file} >> test.csv
        # echo "" >> test.csv
    fi
done
for file in benchmarks/arithmetic/*.blif
do
    if test -f $file ;
    then
        name=`basename $file`
        filename="${name%%.*}"
        echo ${filename}
        echo "${filename},\c" >> test.csv
        ./why/main ${file} >> test.csv
        # echo  "" >> test.csv
    fi
done

# ./abc/abc -c "read_blif benchmarks/arithmetic/div.blif;
#     strash;
#     dc2 -v;"