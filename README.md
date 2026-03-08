# PARSIR

PARSIR (PARallel SImulation Runner) is a compile/runtime environment for discrete event 
simulation models written using the C programming language

The directory models/phold provides and implementation of the PHOLD benchmark that
constitutes a common example of how to realize simulation applications using PARSIR

The building of the application is based on the following API:

a) int ScheduleNewEvent(int destination, double timestamp, int event_type, char* body, int size)
which allows inserting an event for a destination simulation object, that will occur at a given
simulation time, into the PARSIR runtime

b) void ProcessEvent(unsigned int me, double now, int event_type, void *event_content, unsigned int size, void *ptr)
a callback that enables processing an event occurring at a given time in some specific 
simulation object - this callback is initially called with a runtime injected INIT event
occurring at simulation time zero in order to setup in memory the state of each simulation object
and to enable the initial scheduling of new events

To use PARSIR you can simply go to the build directory to compile the runtime or the phold target

After compiling the phold target, the PARSIR-simulator for running the PHOLD model 
will be available in the bin directory and can be simply launched

The include directory contains the file run.h which determines how many simulation objects will
belong to the model as well as how many threads will be started up by the PARSIR-simulator, 
and the lookahead of the simulation model to be executed. Currently they are specified as:
#define THREADS (8) 
#define OBJECTS (1024) 
#define LOOKAHEAD (1.0)

The include directory also holds other .h files where the specification of macros
for managing/building the PARSIR-simulator is defined, such as the maximum number of 
NUMA nodes to be managed, as well as the the maximum number of CPUs per NUMA node

Parallel execution of the simulation objects is carried out by PARSIR in a fully 
transparent manner to the application level code

REQUIREMENTS: PARSIR requires the numa library and its header to be installed on the system (it compiles with the -lnuma option)

To use PARSIR-GRID_CKPT you can simply go to the build directory to compile runtime_grid_ckpt, the runtime, or phold_grid_ckpt, the phold, the berchmark,  target.

# rendi tutti i file eseguibili

find . -type f -not -path './.git/*' -exec chmod +x {} +

# Configurazioni

- BENCHMARK=1 = attiva il benchmark
- PERIOD = durata di ogni campione in secondi
- SAMPLES = numero di campioni raccolti
- phold M quanti eventi iniziali per ogni oggetto
- pcs TA parametro della distribuzione esponenziale
- mmap_mv = usa sempre due slot per pagina: stato corrente e checkpoint
- mmap_mv_store = variante store-only del backend MVM_MMAP_MULTIVERSIONING
- mmap_mv_store_grid = variante store-only con snapshot su griglia
- mmap_mv_store_grid MVMM_GRID_SIZE = dimensione della cella di griglia, default 64

## PHOLD

```bash
make -C build clean
make -C build phold_grid_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build phold_grid_ckpt_bs BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build phold_chunk_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build phold_chunk_full_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build phold_mmap_mv_store BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build phold_mmap_mv_store_grid BENCHMARK=1 PERIOD=5 SAMPLES=5 MVMM_GRID_SIZE=64
./bin/PARSIR-simulator
```

## PCS

```bash
make -C build clean
make -C build pcs_grid_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build pcs_grid_ckpt_bs BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build pcs_chunk_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build pcs_chunk_full_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build pcs_mmap_mv_store BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build pcs_mmap_mv_store_grid BENCHMARK=1 PERIOD=5 SAMPLES=5 MVMM_GRID_SIZE=64
./bin/PARSIR-simulator
```

## HIGHWAY

```bash
make -C build clean
make -C build highway_grid_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build highway_grid_ckpt_bs BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build highway_chunk_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build highway_chunk_full_ckpt BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build highway_mmap_mv_store BENCHMARK=1 PERIOD=5 SAMPLES=5
./bin/PARSIR-simulator

make -C build clean
make -C build highway_mmap_mv_store_grid BENCHMARK=1 PERIOD=5 SAMPLES=5 MVMM_GRID_SIZE=64
./bin/PARSIR-simulator
```

./run_selected_benchmarks.sh
