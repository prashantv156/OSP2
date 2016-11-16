#!/bin/sh
pwd
#cd /usr/local/include/
#pwd
#sudo mkdir keyvalue
#cd -
cd kernel_module/
pwd
sudo make clean
sudo rmmod keyvalue
sudo make
sudo make install
sudo insmod keyvalue.ko
cd ../library/
pwd
sudo make clean
sudo make all
sudo make install
cd ../benchmark/
pwd
sudo make clean
export LD_LIBRARY_PATH=/usr/local/lib
sudo make all
sudo ldconfig
lsmod | grep "keyvalue"
