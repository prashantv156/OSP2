#!usr/bin/perl

my $i	= 0;

for($i=0; $i<2000; $i++)
{
	system("sudo ./benchmark 7200 7200 | ./validate 7200");
	sleep(0.05);
}
