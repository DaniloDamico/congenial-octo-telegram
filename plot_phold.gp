set datafile separator comma
set datafile missing "NaN"
set terminal png size 1200,800 font 'Arial,26'
set key inside right top
set grid

COL_CKPT      = 1
COL_THREADS   = 2
COL_LOOKAHEAD = 3
COL_OBJECTS   = 4
COL_M         = 5
COL_TPUT      = 6
COL_TPUT_DELTA= 7
COL_EPOCHS    = 8
COL_EPOCHS_DELTA = 9
COL_RB        = 10
COL_RB_CI     = 11

ckpt_types  = "grid_ckpt chunk_ckpt full_ckpt"
#m_values    = "1 1000"
#spec_windows  = "0.25 0.5 1.0"

set style data yerrorlines

# Define point styles for each checkpoint type
set style line 1 lw 2 pt 6 ps 3.0  # grid_ckpt: filled circle
set style line 2 lw 2 pt 4 ps 3.0  # chunk_ckpt: filled square
set style line 3 lw 2 pt 8 ps 3.0  # full_ckpt: filled triangle

# Loop over objects to generate one plot per objects value
set title sprintf("Phold (M = %s, Speculative Window %s)", ARG1, ARG2)
set xlabel "Threads"
set ylabel "Throughput (events/s)"
set output sprintf("plots/phold/throughput_obj1024_spec_windows%s_m%s.png", ARG2, ARG1)
plot for [c in ckpt_types] 'phold_plot_data.csv' using \
    (strcol(COL_CKPT) eq c ? column(COL_THREADS) : 1/0): \
    (strcol(COL_CKPT) eq c ? column(COL_TPUT) : 1/0): \
    (strcol(COL_CKPT) eq c ? column(COL_TPUT_DELTA) : 1/0) \
    with yerrorlines ls (c eq 'grid_ckpt' ? 1 : (c eq 'chunk_ckpt' ? 2 : 3)) title (c eq 'grid_ckpt' ? 'grid ckpt' : (c eq 'chunk_ckpt' ? 'chunk ckpt' : 'full ckpt'))
unset output
    
set key inside right top
set grid

set title sprintf("Phold (M = %s, Speculative Window %s)", ARG1, ARG2)
set xlabel "Threads"
set ylabel "Rollbacks per Speculation Window"
set output sprintf("plots/phold/rollbacks_per_epoch_obj1024_spec_windows%s_m%s.png", ARG2, ARG1)
set autoscale y

safe_ratio(rb, ep)      = (ep == 0 ? 0 : rb / ep)
safe_ratio_ci(rbci, ep) = (ep == 0 ? 0 : rbci / ep)

plot for [c in ckpt_types] 'phold_plot_data.csv' using \
    (strcol(COL_CKPT) eq c ? column(COL_THREADS) : 1/0) : \
    (strcol(COL_CKPT) eq c ? safe_ratio(column(COL_RB), column(COL_EPOCHS)) : 1/0) : \
    (strcol(COL_CKPT) eq c ? safe_ratio_ci(column(COL_RB_CI), column(COL_EPOCHS)) : 1/0) \
    with yerrorlines \
    ls (c eq 'grid_ckpt' ? 1 : (c eq 'chunk_ckpt' ? 2 : 3)) \
    title (c eq 'grid_ckpt' ? 'grid ckpt' : (c eq 'chunk_ckpt' ? 'chunk ckpt' : 'full ckpt'))

unset output
