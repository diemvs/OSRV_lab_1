#!/bin/bash

#g++ OTP_1.cpp -lpthread -o otp


rm -f input.txt input2.txt output.txt
touch input.txt input2.txt output.txt

base64 /dev/urandom | head -c 2000 > input.txt # gen random file with base64
# echo RimWorld best game ever! > input.txt

./otp  -i input.txt -o output.txt -x 4212 -a 84589 -c 45989 -m 217728 && ./otp  -i output.txt -o input2.txt -x 4212 -a 84589 -c 45989 -m 217728

if cmp -s input.txt input2.txt; then
    printf '\n\nSUCCESS\n'
else
    printf '\n\nFAIL\n'
fi
