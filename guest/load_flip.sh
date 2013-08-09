module=flip-char
device=flip


insmod flip_pci.ko

Major=$(awk '$2=="'$module'" {print $1}' /proc/devices)

mknod -m 666 /dev/${device}0 c $Major 0
echo "done!"

