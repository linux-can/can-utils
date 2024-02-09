#!/bin/bash
# SPDX-License-Identifier: LGPL-2.0-only
# SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

# Arguments: path to the file and the file size
FILEPATH=$1
FILESIZE=$2

# Variable to store the increasing number
counter=0

# XOR pattern (change this to whatever you like)
pattern=0xdeadbeef

# Calculate the number of iterations needed
let iterations=$FILESIZE/4

# Use 'dd' command to generate the file with increasing numbers
for ((i=0; i<$iterations; i++)); do
    # Print the 32-bit number in binary format to the file
    printf "%08x" $((counter ^ pattern)) | xxd -r -p >> $FILEPATH
    let counter=counter+1
done

