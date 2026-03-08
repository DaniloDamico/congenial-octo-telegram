MAX_MEMORY = 0x200000UL
MOD = 8
GRID_CKPT_FLAGS = -pie -fPIE -fstack-protector-all -DGRID_CKPT -DMOD=$(MOD) -DSPECULATION
CHUNK_CKPT_FLAGS = -pie -fPIE -fstack-protector-all -DSPECULATION -DCHUNK_BASED -DCHUNK_BASED_SAVE
CHUNK_CKPT_FULL_FLAGS = -DSPECULATION -DCHUNK_BASED -DCHUNK_FULL
MMAP_MV_RT_FLAGS = -pie -fPIE -fstack-protector-all -DSPECULATION -DMMAP_MV
MMAP_MV_MODEL_FLAGS = -pie -fpie -I../MVM_MMAP_MULTIVERSIONING/include -DSPECULATION -DMMAP_MV
MMAP_MV_STORE_RT_FLAGS = $(MMAP_MV_RT_FLAGS) -DMMAP_MV_STORE
MMAP_MV_STORE_MODEL_FLAGS = $(MMAP_MV_MODEL_FLAGS) -DMMAP_MV_STORE
MMAP_MV_STORE_GRID_RT_FLAGS = $(MMAP_MV_STORE_RT_FLAGS) -DMMAP_MV_STORE_GRID
MMAP_MV_STORE_GRID_MODEL_FLAGS = $(MMAP_MV_STORE_MODEL_FLAGS) -DMMAP_MV_STORE_GRID
MMAP_MV_PAGE_SIZE ?= 4096
MVMM_GRID_SIZE ?= 64
MMAP_MV_BACKEND_FLAGS = -DMVMM_PAGE_SIZE=$(MMAP_MV_PAGE_SIZE)
MMAP_MV_STORE_BACKEND_FLAGS = -DMMAP_MV_STORE -DMVMM_PAGE_SIZE=$(MMAP_MV_PAGE_SIZE)
MMAP_MV_STORE_GRID_BACKEND_FLAGS = $(MMAP_MV_STORE_BACKEND_FLAGS) -DMMAP_MV_STORE_GRID -DMVMM_GRID_SIZE=$(MVMM_GRID_SIZE)

PHOLD_TARGET_FUNCTIONS = \""<add1>:","<dell1>:","<add2>:","<dell2>:","<read1>:","<read2>:","<update1>:","<update2>:","<ProcessEvent>:"\"
PHOLD_TARGET_MODULES = ../models/phold/application.c

PCS_TARGET_FUNCTIONS = \""<ProcessEvent>:","<allocation>:","<deallocation>:","<fading_recheck>:"\"
PCS_TARGET_MODULES = ../models/pcs/application.c
PCS_LIBS = "-lm -lpthread -lnuma"

HIGHWAY_TARGET_FUNCTIONS = \""<add_car>:","<del_car>:","<traversal>:","<ProcessEvent>:"\"
HIGHWAY_TARGET_MODULES = ../models/highway/application.c

#Benchmarking
PERIOD=5
SAMPLES=30

ifeq ($(BENCHMARK),1)
BENCH_OPT := -DBENCHMARKING -DPERIOD=$(PERIOD) -DSAMPLES=$(SAMPLES) -lm
ifneq ($(strip $(THREADS)),)
BENCH_OPT += -DTHREADS=$(THREADS)
endif
ifneq ($(strip $(LOOKAHEAD)),)
BENCH_OPT += -DLOOKAHEAD=$(LOOKAHEAD)
endif
ifneq ($(strip $(OBJECTS)),)
BENCH_OPT += -DOBJECTS=$(OBJECTS)
endif
else
BENCH_OPT :=
endif

ifeq ($(AVOID_THROTTLING),1)
BENCH_OPT += -DAVOID_THROTTLING
endif

all: runtime
	@ echo "The runtime environment of PARSIR has been compiled - object files are available in the lib directory"

runtime:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include 
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include 
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include 
	ld  -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o -I../include -o ../lib/PARSIR-runtime.o

runtime_grid_ckpt:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include  $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../grid_ckpt/grid_ckpt.c -o ../lib/grid_ckpt.o -I../include -I../MVM_GRID_CKPT/include -DMAX_MEMORY=$(MAX_MEMORY) $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	ld -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/grid_ckpt.o ../lib/speculation.o -I../include -o ../lib/PARSIR-runtime.o

runtime_grid_ckpt_bs:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../grid_ckpt/grid_ckpt.c -o ../lib/grid_ckpt.o -I../include -I../MVM_GRID_CKPT_BS/include -DMAX_MEMORY=$(MAX_MEMORY) $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(GRID_CKPT_FLAGS) $(BENCH_OPT)
	ld -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/grid_ckpt.o ../lib/speculation.o -I../include -I../MVM_GRID_CKPT_BS/include -o ../lib/PARSIR-runtime.o

runtime_chunk_ckpt:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../chunk_ckpt/chunk_ckpt.c -o ../lib/chunk_ckpt.o -I../include $(CHUNK_CKPT_FLAGS) -DMAX_MEMORY=$(MAX_MEMORY) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(CHUNK_CKPT_FLAGS) $(BENCH_OPT)
	ld  -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/chunk_ckpt.o ../lib/speculation.o -I../include -o ../lib/PARSIR-runtime.o

runtime_chunk_full_ckpt:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT) 
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT) 
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT) 
	gcc -c -O3 ../chunk_ckpt/chunk_ckpt.c -o ../lib/chunk_ckpt.o -I../include $(CHUNK_CKPT_FULL_FLAGS) -DMAX_MEMORY=$(MAX_MEMORY) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(CHUNK_CKPT_FULL_FLAGS) $(BENCH_OPT)
	ld  -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/chunk_ckpt.o ../lib/speculation.o -I../include -o ../lib/PARSIR-runtime.o

runtime_mmap_mv_store:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(MMAP_MV_STORE_RT_FLAGS) $(BENCH_OPT)
	ld -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/speculation.o -I../include -o ../lib/PARSIR-runtime.o

runtime_mmap_mv:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(MMAP_MV_RT_FLAGS) $(BENCH_OPT)
	ld -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/speculation.o -I../include -o ../lib/PARSIR-runtime.o

runtime_mmap_mv_store_grid:
	cd ../scripts ; make get-HW-info
	@if [ ! -d ../lib ]; then mkdir -p ../lib; fi
	gcc -c -O3 ../queue/queue.c -o ../lib/queue.o -I../include $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	gcc -c ../queue/barrier-synchronization.c -o ../lib/barrier-synchronization.o -I../include $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../memory/memory.c -o ../lib/memory.o -I../include $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../stubs/stubs.c -o ../lib/stubs.o -I../include $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../random/random.c -o ../lib/random.o -I../include $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../engine/engine.c -o ../lib/engine.o -I../include $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	gcc -c -O3 ../speculation/speculation.c -o ../lib/speculation.o -I../include -pthread $(MMAP_MV_STORE_GRID_RT_FLAGS) $(BENCH_OPT)
	ld -r ../lib/queue.o ../lib/memory.o ../lib/barrier-synchronization.o ../lib/stubs.o ../lib/random.o ../lib/engine.o ../lib/speculation.o -I../include -o ../lib/PARSIR-runtime.o

phold: runtime
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../models/phold ; make
	gcc -o ../bin/PARSIR-simulator ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma

pcs: runtime
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../models/pcs ; make
	gcc -o ../bin/PARSIR-simulator ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma ../models/pcs/pcs.o -lm

highway: runtime
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../models/highway ; make
	gcc -g -o ../bin/PARSIR-simulator ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma ../models/highway/highway.o

phold_grid_ckpt: runtime_grid_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_GRID_CKPT ; make checks backup-files file-rewriting TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/phold ; make grid_ckpt OBJECTS=$(OBJECTS) M=$(M)
	ld -r ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -I../MVM_GRID_CKPT/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_GRID_CKPT ; make compile-and-link restore-files MOD=$(MOD) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_GRID_CKPT CKPT applied to PARSIR

pcs_grid_ckpt: runtime_grid_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_GRID_CKPT ; make checks backup-files file-rewriting TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) LIBS=$(PCS_LIBS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/pcs ; make grid_ckpt OBJECTS=$(OBJECTS) TA=$(TA)
	ld -r ../models/pcs/pcs.o ../lib/PARSIR-runtime.o -I../include -I../MVM_GRID_CKPT/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_GRID_CKPT ; make compile-and-link restore-files MOD=$(MOD) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) LIBS=$(PCS_LIBS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_GRID_CKPT CKPT applied to PARSIR

highway_grid_ckpt: runtime_grid_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_GRID_CKPT ; make checks backup-files file-rewriting TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/highway ; make grid_ckpt
	ld -r ../models/highway/highway.o ../lib/PARSIR-runtime.o -I../include -I../MVM_GRID_CKPT/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_GRID_CKPT ; make compile-and-link restore-files MOD=$(MOD) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_GRID_CKPT CKPT applied to PARSIR

phold_grid_ckpt_bs: runtime_grid_ckpt_bs
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_GRID_CKPT_BS ; make checks backup-files file-rewriting TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/phold ; make grid_ckpt_bs OBJECTS=$(OBJECTS) M=$(M)
	ld -r ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -I../MVM_GRID_CKPT_BS/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_GRID_CKPT_BS ; make compile-and-link restore-files MOD=$(MOD) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_GRID_CKPT_BS applied to PARSIR

pcs_grid_ckpt_bs: runtime_grid_ckpt_bs
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_GRID_CKPT_BS ; make checks backup-files file-rewriting TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) LIBS=$(PCS_LIBS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/pcs ; make grid_ckpt_bs
	ld -r ../models/pcs/pcs.o ../lib/PARSIR-runtime.o -I../include -I../MVM_GRID_CKPT_BS/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_GRID_CKPT_BS ; make compile-and-link restore-files MOD=$(MOD) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) LIBS=$(PCS_LIBS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_GRID_CKPT_BS applied to PARSIR

highway_grid_ckpt_bs: runtime_grid_ckpt_bs
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_GRID_CKPT_BS ; make checks backup-files file-rewriting TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/highway ; make grid_ckpt_bs
	ld -r ../models/highway/highway.o ../lib/PARSIR-runtime.o -I../include -I../MVM_GRID_CKPT_BS/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_GRID_CKPT_BS ; make compile-and-link restore-files MOD=$(MOD) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_GRID_CKPT_BS applied to PARSIR

phold_chunk_ckpt: runtime_chunk_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_CHUNK ; make checks backup-files file-rewriting TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/phold ; make chunk_ckpt OBJECTS=$(OBJECTS) M=$(M)
	ld -r ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -I../MVM_CHUNK/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_CHUNK ; make compile-and-link restore-files TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_CHUNK applied to PARSIR

pcs_chunk_ckpt: runtime_chunk_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_CHUNK ; make checks backup-files file-rewriting TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) LIBS=$(PCS_LIBS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/pcs ; make chunk_ckpt OBJECTS=$(OBJECTS) TA=$(TA)
	ld -r ../models/pcs/pcs.o ../lib/PARSIR-runtime.o -I../include -I../MVM_CHUNK/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_CHUNK ; make compile-and-link restore-files TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) LIBS=$(PCS_LIBS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_CHUNK applied to PARSIR

highway_chunk_ckpt: runtime_chunk_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_CHUNK ; make checks backup-files file-rewriting TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	cd ../models/highway ; make chunk_ckpt
	ld -r ../models/highway/highway.o ../lib/PARSIR-runtime.o -I../include -I../MVM_CHUNK/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_CHUNK ; make compile-and-link restore-files TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) ALLOCATOR_AREA_SIZE=$(MAX_MEMORY) MOD=$(MOD)
	@echo MVM_CHUNK applied to PARSIR

phold_chunk_full_ckpt: runtime_chunk_full_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../models/phold ; make chunk_full_ckpt OBJECTS=$(OBJECTS) M=$(M)
	gcc -o ../bin/PARSIR-simulator ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma -lm

pcs_chunk_full_ckpt: runtime_chunk_full_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../models/pcs ; make chunk_full_ckpt OBJECTS=$(OBJECTS) TA=$(TA)
	gcc -o ../bin/PARSIR-simulator ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma ../models/pcs/pcs.o -lm

highway_chunk_full_ckpt: runtime_chunk_full_ckpt
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../models/highway ; make chunk_full_ckpt
	gcc -g -o ../bin/PARSIR-simulator ../lib/PARSIR-runtime.o -I../include -lpthread -lnuma ../models/highway/highway.o -lm

phold_mmap_mv_store: runtime_mmap_mv_store
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_BACKEND_FLAGS)"
	cd ../models/phold ; make mmap_mv_store OBJECTS=$(OBJECTS) M=$(M)
	ld -r ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING store-only applied to PARSIR

pcs_mmap_mv_store: runtime_mmap_mv_store
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_BACKEND_FLAGS)"
	cd ../models/pcs ; make mmap_mv_store OBJECTS=$(OBJECTS) TA=$(TA)
	ld -r ../models/pcs/pcs.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING store-only applied to PCS

highway_mmap_mv_store: runtime_mmap_mv_store
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_BACKEND_FLAGS)"
	cd ../models/highway ; make mmap_mv_store
	ld -r ../models/highway/highway.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING store-only applied to HIGHWAY

phold_mmap_mv: runtime_mmap_mv
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_BACKEND_FLAGS)"
	cd ../models/phold ; make mmap_mv OBJECTS=$(OBJECTS) M=$(M)
	ld -r ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING base applied to PARSIR

pcs_mmap_mv: runtime_mmap_mv
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_BACKEND_FLAGS)"
	cd ../models/pcs ; make mmap_mv OBJECTS=$(OBJECTS) TA=$(TA)
	ld -r ../models/pcs/pcs.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING base applied to PCS

highway_mmap_mv: runtime_mmap_mv
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_BACKEND_FLAGS)"
	cd ../models/highway ; make mmap_mv
	ld -r ../models/highway/highway.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING base applied to HIGHWAY

phold_mmap_mv_store_grid: runtime_mmap_mv_store_grid
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_GRID_BACKEND_FLAGS)"
	cd ../models/phold ; make mmap_mv_store_grid OBJECTS=$(OBJECTS) M=$(M)
	ld -r ../models/phold/phold.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(PHOLD_TARGET_MODULES) TARGET_FUNCTIONS=$(PHOLD_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_GRID_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING store-grid applied to PARSIR

pcs_mmap_mv_store_grid: runtime_mmap_mv_store_grid
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_GRID_BACKEND_FLAGS)"
	cd ../models/pcs ; make mmap_mv_store_grid OBJECTS=$(OBJECTS) TA=$(TA)
	ld -r ../models/pcs/pcs.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(PCS_TARGET_MODULES) TARGET_FUNCTIONS=$(PCS_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_GRID_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING store-grid applied to PCS

highway_mmap_mv_store_grid: runtime_mmap_mv_store_grid
	@if [ ! -d ../bin ]; then mkdir -p ../bin; fi
	cd ../MVM_MMAP_MULTIVERSIONING ; make checks backup-files file-rewriting TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_GRID_BACKEND_FLAGS)"
	cd ../models/highway ; make mmap_mv_store_grid
	ld -r ../models/highway/highway.o ../lib/PARSIR-runtime.o -I../include -I../MVM_MMAP_MULTIVERSIONING/include -o ../bin/PARSIR-simulator.o
	cd ../MVM_MMAP_MULTIVERSIONING ; make compile-and-link restore-files TARGET_MODULES=$(HIGHWAY_TARGET_MODULES) TARGET_FUNCTIONS=$(HIGHWAY_TARGET_FUNCTIONS) OBJECTS=$(OBJECTS) EXTRA_FLAGS="$(MMAP_MV_STORE_GRID_BACKEND_FLAGS)"
	@echo MVM_MMAP_MULTIVERSIONING store-grid applied to HIGHWAY

clean:
	find .. -name "*.o" -type f -delete ; rm -f ../bin/PARSIR-simulator ; cd ../MVM_GRID_CKPT ; make clean ; cd ../MVM_GRID_CKPT_BS ; make clean ; cd ../MVM_CHUNK ; make clean ; cd ../MVM_MMAP_MULTIVERSIONING ; make clean
