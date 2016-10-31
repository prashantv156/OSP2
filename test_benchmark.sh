sudo rm foo.txt
sudo ./benchmark  10 10 > foo.txt
sudo cat foo.txt | ./validate 10
