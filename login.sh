#!/usr/bin/expect -f

spawn ssh Debanjan-Pritkumar@10.5.18.163 -p 12006

expect "Debanjan-Pritkumar@10.5.18.163's password: "

send -- "bobprit1448\r"

interact