
# iterate from 1 t 1024 touched cachelines
sudo bash -c 'for (( n=1; n<=1024; n*=2 )); do echo "*** num-cache-lines: $n ****" ; chrt -r 1 taskset 0x8000 perf stat  -B -e  L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads,dTLB-load-misses,dTLB-loads,instructions,branches,cs,faults   ./l1-basher -n $n -d 10 -T 1 ; done ' 


