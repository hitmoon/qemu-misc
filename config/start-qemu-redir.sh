#!/bin/sh 
 ~/qemu/build/x86_64-softmmu/qemu-system-x86_64 -enable-kvm -m 2048 -drive file=debian.img,format=raw -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=tcp:127.0.0.1:5567-:22
