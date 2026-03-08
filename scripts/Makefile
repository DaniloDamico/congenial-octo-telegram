get-HW-info:
	@if [ ! -d ../setup-data ]; then mkdir -p ../setup-data; fi
	lscpu | grep -i numa > ../setup-data/hw.txt 
	./remove-file

get-CPUs:
	@ nproc --all
