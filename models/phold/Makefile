CC = gcc 
CFLAGS = -c -Wall -Wextra -Wpedantic -I../../include/ -I./ 
CHUNK_CKPT_FLAGS = -pie -fpie -I../../MVM_CHUNK/include -DSPECULATION
CHUNK_FULL_CKPT_FLAGS = -DSPECULATION
GRID_CKPT_FLAGS = -pie -fpie -I../../MVM_GRID_CKPT/include -DSPECULATION
GRID_CKPT_BS_FLAGS = -pie -fpie -I../../MVM_GRID_CKPT_BS/include -DSPECULATION
TARGET = intermediate-phold.o 

SRCS = application.c 

ifeq ($(OBJECTS),)
OBJ_FLAG :=
else
OBJ_FLAG := -DOBJECTS=$(OBJECTS)
endif

ifeq ($(M),)
M_FLAG :=
else
M_FLAG := -DM=$(M)
endif

all: 
	$(CC) $(SRCS) $(CFLAGS) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o phold.o

chunk_ckpt:
	$(CC) $(SRCS) $(CFLAGS) $(CHUNK_CKPT_FLAGS) $(OBJ_FLAG) $(M_FLAG) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o phold.o

chunk_full_ckpt:
	$(CC) $(SRCS) $(CFLAGS) $(CHUNK_FULL_CKPT_FLAGS) $(OBJ_FLAG) $(M_FLAG) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o phold.o

grid_ckpt:
	$(CC) $(SRCS) $(CFLAGS) $(GRID_CKPT_FLAGS) $(OBJ_FLAG) $(M_FLAG) -o $(TARGET) 
	ld -r $(TARGET) --wrap=malloc --wrap=free -o phold.o

grid_ckpt_bs:
	$(CC) $(SRCS) $(CFLAGS) $(GRID_CKPT_BS_FLAGS) $(OBJ_FLAG) $(M_FLAG) -o $(TARGET)
	ld -r $(TARGET) --wrap=malloc --wrap=free -o phold.o

clean:
	$(RM) *.o $(TARGET)
