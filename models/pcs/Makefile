CC = gcc 
CFLAGS = -c -Wall -Wextra -Wpedantic -I../../include/ -I./ -lm -Wimplicit-fallthrough
CHUNK_CKPT_FLAGS = -pie -fpie -I../../MVM_CHUNK/include -DSPECULATION
CHUNK_FULL_CKPT_FLAGS = -DSPECULATION
GRID_CKPT_FLAGS = -pie -fpie -I../../MVM_GRID_CKPT/include -DSPECULATION
GRID_CKPT_BS_FLAGS = -pie -fpie -I../../MVM_GRID_CKPT_BS/include -DSPECULATION
TARGET1 = intermediate-pcs1.o 
TARGET2 =  
TARGET3 = intermediate-pcs3.o 

SRCS1 = application.c 
#SRCS2 = functions-app.c 
SRCS2 =  
SRCS3 = topology.c 

ifeq ($(OBJECTS),)
OBJ_FLAG :=
else
OBJ_FLAG := -DOBJECTS=$(OBJECTS)
endif

ifeq ($(TA),)
TA_FLAG :=
else
TA_FLAG := -DTA=$(TA)
endif

all: 
	$(CC) $(SRCS1) $(CFLAGS) -o $(TARGET1)
	$(CC) $(SRCS3) $(CFLAGS) -o $(TARGET3)
	ld -r $(TARGET1) $(TARGET2) $(TARGET3) --wrap=malloc --wrap=free -o pcs.o

chunk_ckpt:
	$(CC) $(SRCS1) $(CFLAGS) $(CHUNK_CKPT_FLAGS) $(OBJ_FLAG) $(TA_FLAG) -o $(TARGET1)
	$(CC) $(SRCS3) $(CFLAGS) $(CHUNK_CKPT_FLAGS) $(OBJ_FLAG) $(TA_FLAG) -o $(TARGET3)
	ld -r $(TARGET1) $(TARGET2) $(TARGET3) --wrap=malloc --wrap=free -o pcs.o

chunk_full_ckpt:
	$(CC) $(SRCS1) $(CFLAGS) $(CHUNK_FULL_CKPT_FLAGS) $(OBJ_FLAG) $(TA_FLAG) -o $(TARGET1)
	$(CC) $(SRCS3) $(CFLAGS) $(CHUNK_FULL_CKPT_FLAGS) $(OBJ_FLAG) $(TA_FLAG) -o $(TARGET3)
	ld -r $(TARGET1) $(TARGET2) $(TARGET3) --wrap=malloc --wrap=free -o pcs.o

grid_ckpt:
	$(CC) $(SRCS1) $(CFLAGS) $(GRID_CKPT_FLAGS) $(OBJ_FLAG) $(TA_FLAG) -o $(TARGET1)
	$(CC) $(SRCS3) $(CFLAGS) $(GRID_CKPT_FLAGS) $(OBJ_FLAG) $(TA_FLAG) -o $(TARGET3)
	ld -r $(TARGET1) $(TARGET2) $(TARGET3) --wrap=malloc --wrap=free -o pcs.o

grid_ckpt_bs:
	$(CC) $(SRCS1) $(CFLAGS) $(GRID_CKPT_BS_FLAGS) -o $(TARGET1)
	$(CC) $(SRCS3) $(CFLAGS) $(GRID_CKPT_BS_FLAGS) -o $(TARGET3)
	ld -r $(TARGET1) $(TARGET2) $(TARGET3) --wrap=malloc --wrap=free -o pcs.o

clean:
	$(RM) *.o $(TARGET)
