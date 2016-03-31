#!/bin/sh 
sudo ./qemu-ifup.sh tap0
~/qemu/build/x86_64-softmmu/qemu-system-x86_64 -enable-kvm -m 2048 -drive file=debian.img,format=raw -device e1000,netdev=net0 -netdev tap,id=net0,ifname=tap0,script=no,downscript=no
sudo ./qemu-ifdown.sh tap0
