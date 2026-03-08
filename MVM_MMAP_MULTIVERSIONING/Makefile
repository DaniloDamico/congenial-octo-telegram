
#APPLICATION SPECIFIC STUFF
#the below specify the directory where the user application is located, the name of the executable to be generated, the name of the object file of the program and the names of the target functions to instrument 
#IMPORTANT: a Makefile in the APP dir is responsible for generating the .o file specified by APP_OBJ
BACKEND_DIR := $(CURDIR)

APP = $(BACKEND_DIR)/../bin
EXECUTABLE = $(APP)/PARSIR-simulator
APP_OBJ = $(APP)/PARSIR-simulator.o
#TARGET_MODULES= a.c  sub-dir/b.c this is an example usage
TARGET_MODULES=
#TARGET_FUNCTIONS="<a>:","<b>:" this is an example usage
TARGET_FUNCTIONS=
LIBS = -lpthread -lnuma -lm

ifneq ($(strip $(OBJECTS)),)
OBJECTS_FLAG = -DOBJECTS=$(OBJECTS)
else
OBJECTS_FLAG =
endif

INCLUDE = $(BACKEND_DIR)/include
OBJ = $(BACKEND_DIR)/obj
LIB = $(BACKEND_DIR)/lib
TEMP = $(BACKEND_DIR)/temp
DISASSEMBLY = $(TEMP)/disassembly
DISASSEMBLY_FILE = -Ddisassembly_file=\"$(DISASSEMBLY)\"
TEMPDIR = -Dtemp_dir=$(TEMP)
SHADOW = shadow
INTERMEDIATEFILE = intermediate.S
INTERMEDIATEOBJFILE = intermediate.o
TEMPFILE = -Dtemp_file=\"$(TEMP)/$(INTERMEDIATEFILE)\"
TEMPOBJ = -Dtemp_obj_file=\"$(TEMP)/$(INTERMEDIATEOBJFILE)\"
TF=-Dtarget_functions=\"$(TARGET_FUNCTIONS)\"

#USER DEFINED STUFF - PLEASE CHANGE WHATEVER YOU NEED
USER_DEFINED = $(BACKEND_DIR)/user-defined
USER_DEFINED_FILE = intermediate.S
USER_DEFINED_OBJ_FILE = intermediate.o
UDTEMPDIR = -Duser_defined_dir=\"$(USER_DEFINED)\"
UDTEMPFILE = -Duser_defined_temp_file=\"$(USER_DEFINED)/$(USER_DEFINED_FILE)\"
UDTEMPOBJ = -Duser_defined_temp_obj_file=\"$(USER_DEFINED)/$(USER_DEFINED_OBJ_FILE)\"

SECURITY_FLAGS = -pie -fPIE -fstack-protector-all
EXTRA_FLAGS ?=

ADDITIONAL_FLAGS = -O3 -DASM_PREAMBLE -DAPPLY_PATCHES $(EXTRA_FLAGS)
WRAP_FLAGS = -Xlinker --wrap=main -Xlinker --wrap=mmap

ifneq ($(findstring -DMMAP_MV_STORE,$(EXTRA_FLAGS)),)
WRAP_FLAGS += -Xlinker --wrap=memcpy
endif
# -DVERBOSE
#NOTE: 
#the ASM_PREAMBLE macro enables building a demo patch for each memory access instruction
#which passess control to a trampoline that calls the the_patch(...) fuction
#offered by the MVM package (see patches/patches.c)
#the APPLY_PATCHES macro actually leads to apply the patches thar are build by the instrumentation process
#the VERBOSE macro simply leads to the massive production of output messages 


THE_VM = -DVM_NAME=\"MVM_MMAP_MULTIVERSIONING\"

all: checks backup-files file-rewriting compile-and-link restore-files

compile-and-link: movm
	gcc $(APP_OBJ) $(LIB)/movm.o -o $(EXECUTABLE) $(SECURITY_FLAGS) $(ADDITIONAL_FLAGS) $(LIBS) -I$(INCLUDE) $(WRAP_FLAGS)
	objdump -Dw $(EXECUTABLE) > $(DISASSEMBLY) 

checks:
	@if [ -d $(APP) ]; then echo ""; else echo "application directory does not exist" ; exit 1; fi
	@./scripts/file-existence $(APP) $(TARGET_MODULES)
	@echo "PLEASE for any compiling error run 'make restore-files'\n"
	@if [ ! -d $(OBJ) ]; then mkdir -p $(OBJ); fi
	@if [ ! -d $(LIB) ]; then mkdir -p $(LIB); fi
	@if [ ! -d $(TEMP) ]; then mkdir -p $(TEMP); fi
	@if [ ! -d $(TEMP)/$(SHADOW) ]; then mkdir -p $(TEMP)/$(SHADOW); fi

base:
	./scripts/temp-dir.sh $(TEMP) $(TEMP)/$(INTERMEDIATEFILE)

head:
	cd ./src; gcc _head.c -c -I$(INCLUDE) $(SECURITY_FLAGS) $(ADDITIONAL_FLAGS) -o $(OBJ)/_head.o

startup:
	cd ./src; gcc _early_start.c -c -I$(INCLUDE) $(SECURITY_FLAGS) $(ADDITIONAL_FLAGS) -o $(OBJ)/_early_start.o

asm-patch:
	cd ./src; gcc _asm_patches.S -c -I$(INCLUDE) $(SECURITY_FLAGS) $(ADDITIONAL_FLAGS) -o $(OBJ)/_asm_patches.o

patch:
	cd ./patches; gcc patches.c -c -I../../include -I$(INCLUDE) $(THE_VM) $(UDTEMPDIR) $(UDTEMPFILE) $(UDTEMPOBJ) $(OBJECTS_FLAG) $(SECURITY_FLAGS) $(ADDITIONAL_FLAGS) -o $(OBJ)/_patches.o
#please rember to insert whatever Makefile in the user-defined directory
	cd $(USER_DEFINED); make

movm: base head startup asm-patch patch 
	cd ./src; gcc _elf_parse.c -c $(TEMPDIR) $(TEMPFILE) $(DISASSEMBLY_FILE) $(TEMPOBJ) $(TF) $(THE_VM) -I$(INCLUDE) $(SECURITY_FLAGS) $(ADDITIONAL_FLAGS) -o $(OBJ)/_elf_parse.o
	cd $(OBJ) ; ld -i *.o -o $(LIB)/movm.o

backup-files: checks
	cd $(APP) ; touch $(EXECUTABLE) ; rm $(EXECUTABLE)
	cd $(APP) ; touch $(APP_OBJ) ; tar -cvf $(TEMP)/$(SHADOW)/backup.tar *
	cp $(TARGET_MODULES) $(TEMP)/$(SHADOW) 

restore-files: 
	cp $(TEMP)/$(SHADOW)/backup.tar $(APP) ; cd $(APP) ; tar -xvf backup.tar ; rm backup.tar
	cp $(TEMP)/$(SHADOW)/*.c $(TARGET_MODULES)

file-rewriting:
	export MVM_TEMP_FILE=$(TEMP)/__temp_file ; ./scripts/file-processor.sh $(APP) $(TARGET_MODULES)

clean:
	find . -name "*.o" -type f -delete ; rm -f $(EXECUTABLE) ; find $(TEMP) -type f -exec rm {} +
