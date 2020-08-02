#!/bin/bash

mkdir dir1
mkdir dir1/dir2
mkdir dir1/dir2/dir3
mkdir dir1/dir2/dir3/dir4
mkdir dir1/dir2/dir3/dir5
mkdir MCMC0683
mkdir MCMC0683/msd_dir1/msd_dir2
mkdir MCMC0683/msd_dir1/msd_dir2/~
mkdir MCMC0683/msd_dir1/msd_dir2/~/~tilde_dir
mkdir dir1/~
mkdir dir1/~/~

echo "hello" > dir1/dir2/file0

./isobusfs_create_test_file.sh dir1/dir2/file1k 1024
./isobusfs_create_test_file.sh dir1/dir2/file1m 1048576

 File and directory names testing
mkdir 'dir1/dir2/special_chars_*?/'
mkdir 'dir1/dir2/unicode_名字'

# Define the long suffix for the filenames
long_suffix="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnop"

# Loop to create 300 files with long names
for count in {1..300}; do
    touch "long_name_${count}_${long_suffix}"
done

touch dir1/dir2/hidden_file
touch dir1/dir2/readonly_file
touch dir1/dir2/executable_file
touch dir1/dir2/no_read_permission_file

# Setting permissions
chmod 444 dir1/dir2/readonly_file  # Read-only file
chmod +x dir1/dir2/executable_file  # Executable file
chmod 000 dir1/dir2/no_read_permission_file  # No read permission for anyone

# Create directories for date problems
mkdir "dir1/dir2/y2000_problem"
mkdir "dir1/dir2/y2038_problem"
mkdir "dir1/dir2/y1979_problem"
mkdir "dir1/dir2/y1980_problem"
mkdir "dir1/dir2/y2107_problem"
mkdir "dir1/dir2/y2108_problem"

# Change timestamps to reflect specific years
# Year 2000
touch -d '2000-01-01 00:00:00' dir1/dir2/y2000_problem

# Year 2038 - beyond 19 January 2038, Unix timestamp issue
touch -d '2038-01-20 00:00:00' dir1/dir2/y2038_problem

# Year 1980 - minimal year supported
touch -d '1979-12-31 23:59:59' dir1/dir2/y1979_problem
touch -d '1980-01-01 00:00:00' dir1/dir2/y1980_problem

# Year 2107 - max year 1980+127
touch -d '2107-12-31 23:59:59' dir1/dir2/y2107_problem
touch -d '2108-01-01 00:00:00' dir1/dir2/y2108_problem

