CC = gcc 
CFLAGS = -c -Wall -Wextra -Wpedantic -I../../include/ -I./ 
CHUNK_CKPT_FLAGS = -pie -fpie -I../../MVM_CHUNK/include -DSPECULATION
CHUNK_FULL_CKPT_FLAGS = -DSPECULATION
GRID_CKPT_FLAGS = -pie -fpie -I../../MVM_GRID_CKPT/include -DSPECULATION
GRID_CKPT_BS_FLAGS = -pie -fpie -I../../MVM_GRID_CKPT_BS/include -DSPECULATION
MMAP_MV_FLAGS = -pie -fpie -I../../MVM_MMAP_MULTIVERSIONING/include -DSPECULATION -DMMAP_MV
MMAP_MV_STORE_FLAGS = $(MMAP_MV_FLAGS) -DMMAP_MV_STORE
MMAP_MV_STORE_GRID_FLAGS = $(MMAP_MV_STORE_FLAGS) -DMMAP_MV_STORE_GRID
TARGET = intermediate-highway.o 

SRCS = application.c 

all: 
	$(CC) $(SRCS) $(CFLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o

chunk_ckpt:
	$(CC) $(SRCS) $(CFLAGS) $(CHUNK_CKPT_FLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o

chunk_full_ckpt:
	$(CC) $(SRCS) $(CFLAGS) $(CHUNK_FULL_CKPT_FLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o

grid_ckpt:
	$(CC) $(SRCS) $(CFLAGS) $(GRID_CKPT_FLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o

grid_ckpt_bs:
	$(CC) $(SRCS) $(CFLAGS) $(GRID_CKPT_BS_FLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o

mmap_mv_store:
	$(CC) $(SRCS) $(CFLAGS) $(MMAP_MV_STORE_FLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o

mmap_mv_store_grid:
	$(CC) $(SRCS) $(CFLAGS) $(MMAP_MV_STORE_GRID_FLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o highway.o


clean:
	$(RM) *.o $(TARGET)
