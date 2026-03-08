#!/bin/bash

declare -a LOOKAHEAD=(0.25 0.5 1.0)
declare -a PHOLD_OBJECTS=(1024)
declare -a M_VALUES=(1 1000) 
declare -a PCS_OBJECTS=(1024)
declare -a TA=(0.4 0.1)

threads=$(getconf _NPROCESSORS_ONLN)

THREADS=(
  $((threads * 25 / 100))
  $((threads * 50 / 100))
  $threads
)

PERIOD=5
SAMPLES=12
RUN=5

rm -r plots
mkdir -p plots/phold
mkdir -p plots/pcs

for l in ${LOOKAHEAD[@]};
do
    for o in ${PHOLD_OBJECTS[@]};
    do
        for m in ${M_VALUES[@]};
        do
            echo "Generate plots for Speculation Window = $l, Objects =$o, M = $m"
            for t in ${THREADS[@]};
            do
                gcc -O3 get_phold_data.c -o get_data -lm -DTHREADS=$t -DSPEC=$l -DOBJECTS=$o -DM=$m -DMODEL=0
                ./get_data
            done
            gnuplot -c plot_phold.gp $m $l
            rm phold_plot_data.csv;
        done
    done
done

for l in ${LOOKAHEAD[@]};
do
    for o in ${PCS_OBJECTS[@]};
    do
        for ta in ${TA[@]};
        do
            echo "Generate plots for Speculation Window = $l, Objects =$o, MIT = $ta"
            for t in ${THREADS[@]};
            do
                gcc -O3 get_pcs_data.c -o get_data -lm -DTHREADS=$t -DSPEC=$l -DOBJECTS=$o -DMIT=$ta
                ./get_data
            done
            gnuplot -c plot_pcs.gp $ta $l
            rm pcs_plot_data.csv;
        done
    done
done

rm get_data
