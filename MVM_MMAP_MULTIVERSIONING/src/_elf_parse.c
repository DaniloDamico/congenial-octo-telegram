#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <limits.h>
#include <ctype.h>

#include "head.h"
#include "elf_parse.h"


void user_defined(instruction_record *, patch *);


uint64_t asl_randomization = 0;
		
char* functions[TARGET_FUNCTIONS];

char buffer[LINE_SIZE];
char prev_line[LINE_SIZE];
char temp_line[LINE_SIZE];//this is for RIP-relative instruction management

void (*address)(void) = _instructions;
instruction_record *instructions;
int target_instructions = 0;

void (*address1)(void) = _patches;
patch *patches;//pointer to the memory block where the patch is built

uint64_t intermediate_zones[SIZE];
uint64_t intermediate_flags[SIZE];
int intermediate_zones_index = -1;

typedef struct _parsed_instruction {
	uint64_t address;
	unsigned long size;
	char pc_relative;
	char control_flow;
	int displacement_size;
	uint64_t target_address;
} parsed_instruction;

typedef struct _direct_branch_ref {
	uint64_t source_address;
	uint64_t target_address;
} direct_branch_ref;

#define MAX_PARSED_INSTRUCTIONS 65536

static parsed_instruction parsed_instructions[MAX_PARSED_INSTRUCTIONS];
static int parsed_instructions_count = 0;

static direct_branch_ref direct_branch_refs[MAX_PARSED_INSTRUCTIONS];
static int direct_branch_refs_count = 0;

void audit_block(instruction_record *the_record){
	printf("instruction record:\n \
			belonging function is %s\n \
			address is %p\n \
			size is %lu\n \
			type (l/s) is %c\n \
			rip relative (y/n) is %c\n \
			effective operand address is %p\n \
			requires indirect jump (y/n) is %c\n \
			indirect jump buffer location is %p\n \
			operation is %s\n \
			source is %s\n \
			destination is %s\n \
			data size is %d\n \
			number of instrumentation instructions %d\n", \
			(char*)the_record->function, \
			(void*)the_record->address, \
			the_record->size, \
			the_record->type, \
			the_record->rip_relative, \
			(void*)the_record->effective_operand_address, \
			the_record->indirect_jump, \
			(void*)the_record->middle_buffer, \
			the_record->op, \
			the_record->source, \
			the_record->dest, \
			the_record->data_size, \
			the_record->instrumentation_instructions \
	       );
	fflush(stdout);
}

char intermediate[LINE_SIZE];
int fd;
//this function is essentially a trampoline for passing control to the user-defined instrumentation function
void build_intermediate_representation(void){

	int i;
	int ret;

	patches = (patch*)address1;//always use this reference for accessing the patch area

	for(i=0;i<target_instructions;i++){
		
		patches[i].functional_instr_size = 0; 

		//just passing through user-defined stuff
		user_defined(&instructions[i], &patches[i]);
	}
}

uint64_t book_intermediate_target(uint64_t instruction_address, unsigned long instruction_size){

	int i;

	for (i = 0; i < intermediate_zones_index; i++){
		if((intermediate_zones[i] >= (instruction_address + instruction_size) - 128) && (intermediate_zones[i] <= (instruction_address + instruction_size) + 127) && (intermediate_flags[i] == 0)){
				intermediate_flags[i] = 1;
				AUDIT 
				printf("found free intermediate zone at index %d - making it busy\n",i);
				return intermediate_zones[i];
			}

	}	
	return 0x0;
}

static inline char *skip_spaces(char *s){
	if (s == NULL){
		return "";
	}
	while (*s && isspace((unsigned char)*s)){
		s++;
	}
	return s;
}

static int count_instruction_bytes(const char *encoded){

	int digits = 0;
	int i;

	for (i = 0; encoded[i] != '\0'; i++){
		if (isxdigit((unsigned char)encoded[i])){
			digits++;
		}
	}

	return digits >> 1;
}

static int is_control_flow_mnemonic(const char *op){

	if (op == NULL || op[0] == '\0'){
		return 0;
	}

	if (op[0] == 'j'){
		return 1;
	}

	if (!strcmp(op, "call") || !strcmp(op, "callq") ||
		!strcmp(op, "loop") || !strcmp(op, "loope") ||
		!strcmp(op, "loopne") || !strcmp(op, "loopnz") ||
		!strcmp(op, "loopz") || !strcmp(op, "ret") ||
		!strcmp(op, "retq")){
		return 1;
	}

	return 0;
}

static int parse_absolute_runtime_address(const char *text, uint64_t *target_address){

	char token[BLOCK];
	int i = 0;

	if (text == NULL || target_address == NULL){
		return 0;
	}

	while (*text && isspace((unsigned char)*text)){
		text++;
	}

	if (*text == '*'){
		return 0;
	}

	while (*text && (isxdigit((unsigned char)*text) || *text == 'x' || *text == 'X') && i < (BLOCK - 1)){
		token[i++] = *text++;
	}
	token[i] = '\0';

	if (i == 0){
		return 0;
	}

	*target_address = strtoull(token, NULL, 16) + asl_randomization;
	return 1;
}

static int parse_comment_runtime_address(const char *line, uint64_t *target_address){

	const char *comment = strchr(line, '#');

	if (comment == NULL){
		return 0;
	}

	return parse_absolute_runtime_address(comment + 1, target_address);
}

static int parse_disassembly_instruction(const char *line, parsed_instruction *out){

	char local_line[LINE_SIZE];
	char *address_token;
	char *encoded_bytes;
	char *decoded_instruction;
	char *saveptr = NULL;
	char *op;
	uint64_t target_address = 0;

	if (line == NULL || out == NULL){
		return 0;
	}

	strncpy(local_line, line, sizeof(local_line) - 1);
	local_line[sizeof(local_line) - 1] = '\0';

	address_token = strtok_r(local_line, ":\t", &saveptr);
	encoded_bytes = strtok_r(NULL, "\t", &saveptr);
	decoded_instruction = strtok_r(NULL, "\t", &saveptr);

	if (address_token == NULL || encoded_bytes == NULL || decoded_instruction == NULL){
		return 0;
	}

	address_token = skip_spaces(address_token);
	decoded_instruction = skip_spaces(decoded_instruction);

	if (address_token[0] == '\0' || decoded_instruction[0] == '\0'){
		return 0;
	}

	out->address = strtoull(address_token, NULL, 16) + asl_randomization;
	out->size = (unsigned long)count_instruction_bytes(encoded_bytes);
	if (out->size == 0){
		return 0;
	}

	out->pc_relative = 'n';
	out->control_flow = 'n';
	out->displacement_size = 0;
	out->target_address = 0x0;

	op = strtok_r(decoded_instruction, " \t", &saveptr);
	if (op == NULL){
		return 0;
	}

	if (strstr(line, "(%rip)") != NULL){
		out->pc_relative = 'y';
		out->displacement_size = 4;
		if (!parse_comment_runtime_address(line, &out->target_address)){
			printf("%s: failed to resolve RIP-relative target in line '%s'\n", VM_NAME, line);
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}

	if (is_control_flow_mnemonic(op)){
		out->control_flow = 'y';
		if (!strcmp(op, "ret") || !strcmp(op, "retq")){
			out->pc_relative = 'n';
			out->displacement_size = 0;
			return 1;
		}

		if (parse_absolute_runtime_address(skip_spaces(saveptr), &target_address)){
			out->pc_relative = 'y';
			out->target_address = target_address;
			if (out->size == 2){
				out->displacement_size = 1;
			}
			else{
				out->displacement_size = 4;
			}
		}
	}

	return 1;
}

static void register_parsed_instruction(const parsed_instruction *instruction){

	if (parsed_instructions_count >= MAX_PARSED_INSTRUCTIONS){
		printf("%s: too many parsed instructions (max %d)\n", VM_NAME, MAX_PARSED_INSTRUCTIONS);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	parsed_instructions[parsed_instructions_count++] = *instruction;

	if (instruction->control_flow == 'y' && instruction->target_address != 0x0){
		if (direct_branch_refs_count >= MAX_PARSED_INSTRUCTIONS){
			printf("%s: too many branch references (max %d)\n", VM_NAME, MAX_PARSED_INSTRUCTIONS);
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
		direct_branch_refs[direct_branch_refs_count].source_address = instruction->address;
		direct_branch_refs[direct_branch_refs_count].target_address = instruction->target_address;
		direct_branch_refs_count++;
	}
}

static int target_record_index_for_address(uint64_t address, int exclude_record){

	int i;

	for (i = 0; i < target_instructions; i++){
		if (i == exclude_record){
			continue;
		}
		if (instructions[i].address == address){
			return i;
		}
	}

	return -1;
}

static int has_external_direct_branch_target(uint64_t range_start, uint64_t range_end){

	int i;

	for (i = 0; i < direct_branch_refs_count; i++){
		if (direct_branch_refs[i].target_address <= range_start ||
			direct_branch_refs[i].target_address >= range_end){
			continue;
		}

		if (direct_branch_refs[i].source_address < range_start ||
			direct_branch_refs[i].source_address >= range_end){
			return 1;
		}
	}

	return 0;
}

static void fail_unsupported_detour(const instruction_record *record, const char *reason, uint64_t detail_address){

	printf("%s: cannot build a detour for %s at %p (%s, detail=%p)\n",
		VM_NAME,
		record->function,
		(void*)record->address,
		reason,
		(void*)detail_address);
	fflush(stdout);
	exit(EXIT_FAILURE);
}

static int configure_short_jump_fallback(instruction_record *record){

	uint64_t intermediate_target = book_intermediate_target(record->address, record->size);

	if (intermediate_target == 0x0){
		return 0;
	}

	record->indirect_jump = 'y';
	record->middle_buffer = intermediate_target;
	record->detour_size = record->size;
	record->stolen_instruction_count = 1;
	record->stolen[0].address = record->address;
	record->stolen[0].size = record->size;
	record->stolen[0].pc_relative = 'n';
	record->stolen[0].control_flow = 'n';
	record->stolen[0].displacement_size = 0;
	record->stolen[0].target_address = 0x0;
	return 1;
}

static void finalize_detours(void){

	int i;
	int j;

	for (i = 0; i < target_instructions; i++){
		unsigned long detour_size = 0;
		uint64_t detour_start = instructions[i].address;
		int parsed_index = instructions[i].parsed_index;
		const char *fallback_reason = NULL;
		uint64_t fallback_detail = 0x0;

		instructions[i].indirect_jump = 'n';
		instructions[i].middle_buffer = 0x0;
		instructions[i].detour_size = instructions[i].size;
		instructions[i].stolen_instruction_count = 0;

		if (instructions[i].size >= 5){
			instructions[i].stolen_instruction_count = 1;
			instructions[i].stolen[0].address = instructions[i].address;
			instructions[i].stolen[0].size = instructions[i].size;
			instructions[i].stolen[0].pc_relative = 'n';
			instructions[i].stolen[0].control_flow = 'n';
			instructions[i].stolen[0].displacement_size = 0;
			instructions[i].stolen[0].target_address = 0x0;
			continue;
		}

		if (parsed_index < 0 || parsed_index >= parsed_instructions_count){
			fallback_reason = "missing parsed instruction metadata";
			goto fallback;
		}

		while (detour_size < 5){
			parsed_instruction *parsed;
			int stolen_index = instructions[i].stolen_instruction_count;

			if ((parsed_index + stolen_index) >= parsed_instructions_count){
				fallback_reason = "detour runs beyond parsed instruction stream";
				goto fallback;
			}
			if (stolen_index >= MAX_STOLEN_INSTRUCTIONS){
				fallback_reason = "too many stolen instructions";
				goto fallback;
			}

			parsed = &parsed_instructions[parsed_index + stolen_index];
			if (parsed->address != (detour_start + detour_size)){
				fallback_reason = "non-contiguous stolen bytes";
				fallback_detail = parsed->address;
				goto fallback;
			}
			if ((detour_size + parsed->size) > MAX_STOLEN_BYTES){
				fallback_reason = "detour exceeds byte budget";
				fallback_detail = parsed->address;
				goto fallback;
			}
			if (stolen_index > 0 && target_record_index_for_address(parsed->address, i) >= 0){
				fallback_reason = "detour would swallow another instrumented access";
				fallback_detail = parsed->address;
				goto fallback;
			}

			instructions[i].stolen[stolen_index].address = parsed->address;
			instructions[i].stolen[stolen_index].size = parsed->size;
			instructions[i].stolen[stolen_index].pc_relative = parsed->pc_relative;
			instructions[i].stolen[stolen_index].control_flow = parsed->control_flow;
			instructions[i].stolen[stolen_index].displacement_size = parsed->displacement_size;
			instructions[i].stolen[stolen_index].target_address = parsed->target_address;

			detour_size += parsed->size;
			instructions[i].stolen_instruction_count++;
		}

		for (j = 0; j < instructions[i].stolen_instruction_count; j++){
			uint64_t target_address = instructions[i].stolen[j].target_address;
			int displacement_size = instructions[i].stolen[j].displacement_size;

			if (instructions[i].stolen[j].control_flow == 'y' && displacement_size == 0){
				fallback_reason = "control-flow instruction without relocatable displacement";
				fallback_detail = instructions[i].stolen[j].address;
				goto fallback;
			}

			if (instructions[i].stolen[j].pc_relative == 'y' && displacement_size != 1 && displacement_size != 4){
				fallback_reason = "unsupported pc-relative displacement size";
				fallback_detail = instructions[i].stolen[j].address;
				goto fallback;
			}

			if (instructions[i].stolen[j].control_flow == 'y' && displacement_size == 1 &&
				(target_address < detour_start || target_address >= (detour_start + detour_size))){
				fallback_reason = "short branch escapes the stolen block";
				fallback_detail = instructions[i].stolen[j].address;
				goto fallback;
			}
		}

		if (has_external_direct_branch_target(detour_start, detour_start + detour_size)){
			fallback_reason = "a direct branch targets bytes inside the stolen block";
			fallback_detail = detour_start;
			goto fallback;
		}

		instructions[i].detour_size = detour_size;
		continue;

fallback:
		if (configure_short_jump_fallback(&instructions[i])){
			continue;
		}
		fail_unsupported_detour(&instructions[i], fallback_reason, fallback_detail);
	}
}

static void relocate_stolen_bytes(instruction_record *record, patch *actual_patch, unsigned long detour_size){

	unsigned long copied_offset = 0;
	int i;

	(void)detour_size;

	for (i = 0; i < record->stolen_instruction_count; i++){
		char *copied_instruction;
		uint64_t target_address;
		int64_t new_displacement;
		unsigned char *displacement_ptr;

		copied_instruction = actual_patch->code + copied_offset;
		target_address = record->stolen[i].target_address;

		if (record->stolen[i].pc_relative != 'y'){
			copied_offset += record->stolen[i].size;
			continue;
		}

		if (target_address >= record->address && target_address < (record->address + record->detour_size)){
			target_address = (uint64_t)(actual_patch->code + (target_address - record->address));
		}

		new_displacement = (int64_t)target_address - (int64_t)((uint64_t)copied_instruction + record->stolen[i].size);
		displacement_ptr = (unsigned char *)(copied_instruction + record->stolen[i].size - record->stolen[i].displacement_size);

		if (record->stolen[i].displacement_size == 4){
			if (new_displacement < INT32_MIN || new_displacement > INT32_MAX){
				fail_unsupported_detour(record, "pc-relative target is out of rel32 range", record->stolen[i].address);
			}
			displacement_ptr[0] = (unsigned char)(new_displacement & 0xff);
			displacement_ptr[1] = (unsigned char)((new_displacement >> 8) & 0xff);
			displacement_ptr[2] = (unsigned char)((new_displacement >> 16) & 0xff);
			displacement_ptr[3] = (unsigned char)((new_displacement >> 24) & 0xff);
		}
		else if (record->stolen[i].displacement_size == 1){
			if (new_displacement < INT8_MIN || new_displacement > INT8_MAX){
				fail_unsupported_detour(record, "pc-relative target is out of rel8 range", record->stolen[i].address);
			}
			displacement_ptr[0] = (unsigned char)(new_displacement & 0xff);
		}
		else{
			fail_unsupported_detour(record, "unsupported pc-relative displacement width", record->stolen[i].address);
		}

		copied_offset += record->stolen[i].size;
	}
}

void build_patches(void){

	int i;
	unsigned long size;
	unsigned long detour_size;
	uint64_t instruction_address;
	int jmp_displacement;
	char *jmp_target;
	char v[128];//this hosts the jmp binary
	int jmp_back_displacement;
	int pos;
	uint64_t intermediate_target;

	uint64_t test_code = (uint64_t)the_patch_assembly;
	int test_code_size = 82;  //this is taken from the compiled version of the src/_asm_patch.S file

	patches = (patch*)address1;

	for (i=0;i<target_instructions;i++){


		//saving original instruction address
		instruction_address = patches[i].original_instruction_address = instructions[i].address;

		//saving original instruction size
		size = instructions[i].size;
		detour_size = patches[i].original_instruction_size = instructions[i].detour_size;
		if (detour_size < 5 && instructions[i].indirect_jump != 'y'){
			fail_unsupported_detour(&instructions[i], "detour smaller than a rel32 jump", instruction_address);
		}

		//saving the original instruction bytes
		memcpy((char*)(patches[i].original_instruction_bytes),(char*)(instructions[i].address),detour_size); 

		patches[i].code = patches[i].block;//you can put wathever instruction in the block of the patch

#ifdef ASM_PREAMBLE
		//copy the asm-patch code into the instructions block
		memcpy((char*)(patches[i].code),(char*)(test_code),test_code_size); 
		//adjust the asm-patch offset for the call to the patch function
		jmp_target = (char*)the_patch;
		jmp_displacement = (int)((char*)jmp_target - ((char*)(patches[i].code) + 50));//this is because we substitute the original call instruction offset with the one observable after the asm-patch instructions copy 
		pos = 0;
        	v[pos++] = 0xe8;
 	       	v[pos++] = (unsigned char)(jmp_displacement & 0xff);
       		v[pos++] = (unsigned char)(jmp_displacement >> 8 & 0xff);
       		v[pos++] = (unsigned char)(jmp_displacement >> 16 & 0xff);
       		v[pos++] = (unsigned char)(jmp_displacement >> 24 & 0xff);
		//memcpy((char*)(patches[i].code) + 36,v,5);

		//CAREFULL THIS
		memcpy((char*)(patches[i].code) + 45,v,5);

		//adjust the parameter to be passed by the asm-patch to the actual patch
		pos = 0;
 	       	v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 8 & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 16 & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 24 & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 32 & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 40 & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 48 & 0xff);
       		v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) >> 56 & 0xff);

		memcpy((char*)(patches[i].code) + 37,v,8);
		patches[i].code = patches[i].code + test_code_size;
#endif
		//copy the original bytes stolen from the target site
		memcpy((char*)(patches[i].code),(char*)(instructions[i].address),detour_size);
		relocate_stolen_bytes(&instructions[i], &patches[i], detour_size);

#ifdef ASM_PREAMBLE
		//move again at the begin of the block of instructions forming the patch
		//NOTE: you will need to have patches[i].code point again to patches[i].block before proceeding with the following if/else	
		patches[i].code = patches[i].code - test_code_size;
#endif

		if (instructions[i].indirect_jump == 'y'){
			intermediate_target = instructions[i].middle_buffer;
			if (intermediate_target == 0x0){
				fail_unsupported_detour(&instructions[i], "missing intermediate landing pad", instruction_address);
			}
			patches[i].intermediate_zone_address = intermediate_target;

			jmp_target = (char*)(patches[i].code);
			jmp_displacement = (int)((char*)jmp_target - ((char*)(intermediate_target)+5));
			pos = 0;
        		v[pos++] = 0xe9;
 	       		v[pos++] = (unsigned char)(jmp_displacement & 0xff);
       			v[pos++] = (unsigned char)(jmp_displacement >> 8 & 0xff);
       			v[pos++] = (unsigned char)(jmp_displacement >> 16 & 0xff);
       			v[pos++] = (unsigned char)(jmp_displacement >> 24 & 0xff);
			memcpy((char*)(patches[i].jmp_to_post),v,5);

			jmp_target = (char*)(intermediate_target);
			jmp_displacement = (signed char)((char*)jmp_target - ((char*)(instruction_address)+2));
			pos = 0;
        		v[pos++] = 0xeb;
 	       		v[pos++] = (unsigned char)(jmp_displacement & 0xff);
			memcpy((char*)(patches[i].jmp_to_intermediate),v,2);
		}
		else{
			AUDIT
			printf("packing a 5-byte jump for instruction with index %d\n",i);
			jmp_target = (char*)(patches[i].code);
			jmp_displacement = (int)((char*)jmp_target - ((char*)(instruction_address)+5));//this is because we substitute the original instruction with a 5-byte relative jmp
			AUDIT
			printf("jump displacement is %d\n",jmp_displacement);
       			 fflush(stdout);
			pos = 0;
        		v[pos++] = 0xe9;
 	       		v[pos++] = (unsigned char)(jmp_displacement & 0xff);
       			v[pos++] = (unsigned char)(jmp_displacement >> 8 & 0xff);
       			v[pos++] = (unsigned char)(jmp_displacement >> 16 & 0xff);
       			v[pos++] = (unsigned char)(jmp_displacement >> 24 & 0xff);
			//record the patch to be applied
			memcpy((char*)(patches[i].jmp_to_post),v,5);
		}

#ifdef ASM_PREAMBLE
		//NOTE: for the below code fragment you will need to have patches[i].code point to the copy of the original instruction - you will need to step forward other preceeding instructions forming the patch
		patches[i].code = patches[i].code + test_code_size;
#endif
		
		memcpy(patches[i].code + detour_size, patches[i].functional_instr, patches[i].functional_instr_size);

		jmp_target = (char*)(instruction_address)+detour_size;

		size = detour_size + patches[i].functional_instr_size;

		jmp_back_displacement = (char*)jmp_target - ((char*)(patches[i].code)+size+5);//here we go beyond the instruction+jmp - but this is a baseline
		AUDIT
		printf("jump back displacement is %d\n",jmp_back_displacement);
		fflush(stdout);
		pos = 0;
        	v[pos++] = 0xe9;
 	       	v[pos++] = (unsigned char)(jmp_back_displacement & 0xff);
       		v[pos++] = (unsigned char)(jmp_back_displacement >> 8 & 0xff);
       		v[pos++] = (unsigned char)(jmp_back_displacement >> 16 & 0xff);
       		v[pos++] = (unsigned char)(jmp_back_displacement >> 24 & 0xff);
		//log the jmp-back into the patch area 
		memcpy((char*)(patches[i].code)+size,(char*)v,5);
	}
}


void apply_patches(void){

	int i;
	unsigned long size;
	unsigned long instruction_address;
	unsigned long instruction_patch;
	unsigned long page_address;
	unsigned long last_page_address;

	for (i=0;i<target_instructions;i++){
		size = patches[i].original_instruction_size;
		instruction_address = instructions[i].address;
		if(patches[i].original_instruction_size == 0){
			continue;
		}
		AUDIT
		printf("applying a patch to instruction with index %d\n",i);
		fflush(stdout);
		if (instructions[i].indirect_jump == 'y'){
			syscall(10, (instruction_address & mask) - 128, PAGE, PROT_READ | PROT_EXEC |PROT_WRITE);
			syscall(10, instruction_address & mask, PAGE, PROT_READ | PROT_EXEC |PROT_WRITE);
			syscall(10, (instruction_address & mask) + PAGE, PAGE, PROT_READ | PROT_EXEC |PROT_WRITE);
			memcpy((char*)instruction_address,(char*)patches[i].jmp_to_intermediate,2);
			memcpy((char*)patches[i].intermediate_zone_address,(char*)patches[i].jmp_to_post,5);
		}
		else{
			instruction_patch = (unsigned long)patches[i].jmp_to_post;
			page_address = instruction_address & mask;
			last_page_address = (instruction_address + size - 1) & mask;
			syscall(10, page_address, PAGE, PROT_READ | PROT_EXEC |PROT_WRITE);
			if (last_page_address != page_address){
				syscall(10, last_page_address, PAGE, PROT_READ | PROT_EXEC |PROT_WRITE);
			}
			memcpy((char*)instruction_address,(char*)instruction_patch,5);
			if (size > 5){
				memset((char*)instruction_address + 5, 0x90, size - 5);
			}
		}
		//original permissions need to be restored - TO DO
	}
}

// the index returned by this function depends on how CPU registes are 
// saved into the stack area when the memory access patch gets executed
// this depends on src/_asm_patches.S
int get_register_index(char* the_register){
	
	if(strcmp(the_register,"%r15") == 0) return 1;
	if(strcmp(the_register,"%r14") == 0) return 2;
	if(strcmp(the_register,"%r13") == 0) return 3;
	if(strcmp(the_register,"%r12") == 0) return 4;
	if(strcmp(the_register,"%rbp") == 0) return 5;
	if(strcmp(the_register,"%rbx") == 0) return 6;
	if(strcmp(the_register,"%r11") == 0) return 7;
	if(strcmp(the_register,"%r10") == 0) return 8;
	if(strcmp(the_register,"%r9") == 0) return 9;
	if(strcmp(the_register,"%r8") == 0) return 10;
	if(strcmp(the_register,"%rax") == 0) return 11;
	if(strcmp(the_register,"%rcx") == 0) return 12;
	if(strcmp(the_register,"%rdx") == 0) return 13;
	if(strcmp(the_register,"%rsi") == 0) return 14;
	if(strcmp(the_register,"%rdi") == 0) return 15;
	return -2;
}

//this function determines the size of touched data based on the instruction source/destination
//type is either 'l' or 's' for load/store instructions
//it i usefull for mov instructions where data size is not explicit
int operands_check(char * source, char * destination, char type){

	char *reg = (type == 's') ? source : destination;
	if(!strcmp(reg,"%eax") || !strcmp(reg,"%ebx") || !strcmp(reg,"%ecx") || !strcmp(reg,"%edx") ||
		!strcmp(reg,"%r8d") || !strcmp(reg,"%r9d") || !strcmp(reg,"%r10d") || !strcmp(reg,"%r11d") ||
		!strcmp(reg,"%r12d") || !strcmp(reg,"%r13d") || !strcmp(reg,"%r14d") || !strcmp(reg,"%r15d") ||
		!strcmp(reg,"%esi") || !strcmp(reg,"%edi") ) return sizeof(int);
	    
	if(!strcmp(reg,"%ax") || !strcmp(reg,"%bx") || !strcmp(reg,"%cx") || !strcmp(reg,"%dx") ||
		!strcmp(reg,"%r8w") || !strcmp(reg,"%r9w") || !strcmp(reg,"%r10w") || !strcmp(reg,"%r11w") ||
		!strcmp(reg,"%r12w") || !strcmp(reg,"%r13w") || !strcmp(reg,"%r14w") || !strcmp(reg,"%r15w") ||
		!strcmp(reg,"%si") || !strcmp(reg,"%di") ) return 2;

	if(!strcmp(reg,"%al") || !strcmp(reg,"%bl") || !strcmp(reg,"%cl") || !strcmp(reg,"%dl") ||
		!strcmp(reg,"%r8b") || !strcmp(reg,"%r9b") || !strcmp(reg,"%r10b") || !strcmp(reg,"%r11b") ||
		!strcmp(reg,"%r12b") || !strcmp(reg,"%r13b") || !strcmp(reg,"%r14b") || !strcmp(reg,"%r15b") ||
		!strcmp(reg,"%sil") || !strcmp(reg,"%dil") ) return sizeof(char);

	return sizeof(unsigned long);//this is the 8-byte default

}

//this function returns the number of bytes to be touched by a given instruction
int get_data_size(char * instruction, char * source, char * dest, char type){

	if(!strcmp(instruction,"movb")) return sizeof(char);
	if(!strcmp(instruction,"movl")) return sizeof(int);
	if(!strcmp(instruction,"movq")) return sizeof(unsigned long);

	if(!strcmp(instruction,"movss")) return sizeof(float);
	if(!strcmp(instruction,"movsd")) return sizeof(double);;

	if(!strcmp(instruction,"movzwl")) return sizeof(float);;
	if(!strcmp(instruction,"movzbl")) return sizeof(float);;
	if(!strcmp(instruction,"movzbw")) return sizeof(int);;

	if(!strcmp(instruction,"movsbl")) return sizeof(int);;

	if(!strcmp(instruction,"mov")) return operands_check(source,dest,type);

	return -1;//unknown instruction 

}

//this returns the number of memory move instructions that have been identified for instrumentation - this number is also written to the target_instructions variable
int elf_parse(char ** function_names, char * parsable_elf){

	int i;
	int j;
	int k;
	int num_functions;
	int offset;
	int len;
	FILE* the_file;
	char* guard;
	char* p;
	char* aux;
	char category;
	char rip_relative;
	char raw_line[LINE_SIZE];
	uint64_t function_start_address;
	uint64_t function_end_address;
	uint64_t instruction_start_address;
	unsigned long instruction_len;
	uint64_t rip_displacement;
	unsigned long target_displacement;
	int register_index;
	long corrector;
	int parsed_index;
	parsed_instruction parsed;
	

	instructions = (instruction_record*)address;

	for (i=0;;i++){//counting functions to parse
	 	if (function_names[i] == NULL) break;
	}
	num_functions = i;
	
	AUDIT
	printf("number of functions to parse: %d\n",num_functions);
	fflush(stdout);

	the_file = fopen(parsable_elf,"r");
	if(!the_file){
		printf("%s: disassembly file %s not accessible\n",VM_NAME,parsable_elf);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	for (i=0;i<num_functions;i++){//parsing all the functions
		AUDIT
		printf("sarching for function %s\n",function_names[i]);
		offset = fseek(the_file,0,SEEK_SET);//moving to the beginning of the ELF file

		while(1){
			guard = fgets(buffer,LINE_SIZE,the_file);
			if(guard == NULL){
			       AUDIT
			       printf("ending cycle for string %s\n",function_names[i]);
			       printf("function to be instrumented %s not found in the executable\n",function_names[i]);
			       exit(EXIT_FAILURE);
		       	       break;
			}
			strtok(buffer,"\n");
			if(strstr(buffer,function_names[i])){
				AUDIT
				printf("found line for function %s\n",function_names[i]);
				strtok(buffer," ");
				AUDIT
				printf("address of function %s is %s\n",function_names[i],buffer);
				function_start_address = (unsigned long)strtol(buffer,NULL,16);
				AUDIT
				printf("numerical address of function %s is %p\n",function_names[i],(void*)function_start_address);

				while(1){
					memcpy(prev_line,buffer,LINE_SIZE);
					guard = fgets(buffer,LINE_SIZE,the_file);
					if (guard != NULL){
						strncpy(raw_line, buffer, sizeof(raw_line) - 1);
						raw_line[sizeof(raw_line) - 1] = '\0';
					}
					parsed_index = -1;
					if (guard != NULL && parse_disassembly_instruction(raw_line, &parsed)){
						register_parsed_instruction(&parsed);
						parsed_index = parsed_instructions_count - 1;
					}
					AUDIT
					printf("%s",buffer);

					if(strcmp(buffer,"\n")==0){
						AUDIT
						printf("found end of function %s\n",function_names[i]);
						strtok(prev_line,":");
						AUDIT
						printf("end address of function %s is %s\n",function_names[i],prev_line);
						function_end_address = (unsigned long)strtol(prev_line,NULL,16);
						AUDIT
						printf("numerical end address of function %s is %p\n",function_names[i],(void*)function_end_address);
						break;
					}
					//now we look at the actual instruction
					strtok(buffer,"\n");
				 	if(strstr(buffer,"mov") && ((strstr(buffer,")"))) && (!(strstr(buffer,"(%rbp,"))) && (!(strstr(buffer,"(%rsp,"))) && (!(strstr(buffer,"(%rbp)"))) && (!(strstr(buffer,"(%rsp)")))){
						AUDIT
						printf("found target data move instruction - line is %s\n",buffer);

						if(target_instructions >= MAX_TARGET_INSTRUCTIONS){
							printf("%s: out of head-memory (max target instructions %d)\n", VM_NAME, MAX_TARGET_INSTRUCTIONS);
							fflush(stdout);
							exit(EXIT_FAILURE);
						}

						if(strstr(buffer,"),")){
							AUDIT
							printf("move from memory (load)\n");
							category = 'l';
#ifdef MMAP_MV_STORE
							continue;
#endif
						}
						else{
							AUDIT
							printf("move to memory (store)\n");
							category = 's';
						}

						if(strstr(buffer,"%rip")){
							AUDIT
							printf("is rip relative\n");
							rip_relative = 'y';
						}
						else{
							AUDIT
							printf("is not rip relative\n");
							rip_relative = 'n';
						}

						/* Instrumenta load/store non RIP-relative.
						 * Gli accessi a stack/globals fuori dalle regioni MVMM
						 * vengono scartati a runtime dal patch stesso.
						 */
						if (rip_relative == 'y') {
							continue;
						}
						strtok(buffer,"#");
						//at this point the instruction line has been removed of \n 
						//and # so that we can tokenize again having already 
						//excluded instruction comments
						AUDIT
						printf("found target data move instruction - line is %s\n",buffer);
						p = strtok(buffer,":\t");

						instruction_start_address = strtol(p,NULL,16) + asl_randomization;
						AUDIT
						printf("instruction is in function %s\n",function_names[i]);
						instructions[target_instructions].function = function_names[i];
						AUDIT
						printf("instruction address is %p\n",(void*)instruction_start_address);
						instructions[target_instructions].address = instruction_start_address;
						instructions[target_instructions].parsed_index = parsed_index;
						//we now simply rewrite 0x0 on the structure that keeps the targeted memory location information
						memset((char*)&(instructions[target_instructions].target),0x0,sizeof(target_address));

						p = strtok(NULL,":\t");
						AUDIT
						printf("instruction binary is %s\n",p);
						instruction_len = 0;
						for(j=0;j<strlen(p);j++){
							if(p[j]!=' ') instruction_len++; 
						}
							instruction_len >>= 1;
							AUDIT
							printf("instruction len is %lu\n",instruction_len);

							instructions[target_instructions].size = instruction_len;
							instructions[target_instructions].detour_size = instruction_len;
							instructions[target_instructions].indirect_jump = 'n';
							instructions[target_instructions].middle_buffer = 0x0;
							instructions[target_instructions].stolen_instruction_count = 0;
						p = strtok(NULL,":\t");
						if(!strstr(p,"mov")) {
							printf("parsing bug, expected 'mov' not foud\n");
							exit(EXIT_FAILURE);
						}
						AUDIT
						printf("%s\n",p);//printing the whole instruction string
						strcpy(instructions[target_instructions].whole,p);
						if(category == 'l') {
							aux = strstr(p,"),");
							*(aux+1) = ' ';

						}
						else{
							aux = strstr(p,",");
							*(aux) = ' ';
						}

						//is it a load or a store?
						instructions[target_instructions].type = category;
						instructions[target_instructions].rip_relative = rip_relative;

						p = strtok(p," ");
						AUDIT
						printf("%s\n",p);//printing mov* 
						strcpy(instructions[target_instructions].op,p);

						p = strtok(NULL," ");
						AUDIT
						printf("%s\n",p);//priting sorce
						strcpy(instructions[target_instructions].source,p);

						p = strtok(NULL," ");
						AUDIT
						printf("%s\n",p);//priting dest 
						strcpy(instructions[target_instructions].dest,p);

						instructions[target_instructions].data_size = -1;
						//determining the size of mouved data
						instructions[target_instructions].data_size = get_data_size(instructions[target_instructions].op,instructions[target_instructions].source,instructions[target_instructions].dest,instructions[target_instructions].type);
						
						//if(!strcmp(instructions[target_instructions].op,"movb")) instructions[target_instructions].data_size = 1;

						instructions[target_instructions].effective_operand_address = 0x0;//this is the default
						//need to fix the rip relative to absolute operand address before ending
						if (rip_relative == 'y' && category == 'l'){
							strcpy(temp_line,instructions[target_instructions].source);

						}
						if (rip_relative == 'y' && category == 's'){
							strcpy(temp_line,instructions[target_instructions].dest);
						}
						if(rip_relative == 'y'){
							strtok(temp_line,"(");

							if(temp_line[0] == '-'){
								rip_displacement = strtoul(temp_line+1,NULL,16);
								corrector = -1;
							}
							else{
								rip_displacement = strtoul(temp_line,NULL,16);
								corrector = 1;
							}
							corrector = corrector * (long)rip_displacement;
							AUDIT
							printf("rip displacement is %p\n",(void*)rip_displacement);
							AUDIT
							printf("displacement value is %ld\n",corrector);
							instructions[target_instructions].effective_operand_address = (unsigned long)((long)((char*)instruction_start_address+instruction_len) + corrector); 
							AUDIT
							printf("effective operand address is %lu\n",instructions[target_instructions].effective_operand_address);

						}	
						else{
							AUDIT
							printf("computing the parameters for the operand address\n");
							fflush(stdout);

							instructions[target_instructions].effective_operand_address = 0x0; 
							if ( category == 'l'){
								strcpy(temp_line,instructions[target_instructions].source);

							}
							if (category == 's'){
								strcpy(temp_line,instructions[target_instructions].dest);
							}
							if(temp_line[0] != '(') {
								AUDIT
								printf("found a displacement\n");
								fflush(stdout);
								strtok(temp_line,"(");
								if(temp_line[0] == '-'){
									target_displacement = strtoul(temp_line+1,NULL,16);
									corrector = -1;
								}
								else{
									target_displacement = strtoul(temp_line,NULL,16);
									corrector = 1;
								}
								instructions[target_instructions].target.displacement = (long)(target_displacement) * (long)corrector; 
								p = aux = strtok(NULL,"(");//go to the next part of the address expression
								strtok(p,",)");
								k = 0;
								do{
									aux = strtok(NULL,",)");//count the arguments
									k++;
								}while(aux);

								switch(k){
									case 1: 
										register_index = get_register_index(p);
										AUDIT
										printf("register index for register %s is %d\n",p,register_index);
										instructions[target_instructions].target.base_index = register_index; 
										break; 
									case 2: break; 
									case 3: 

										register_index = get_register_index(p);
										AUDIT
										printf("register index for base register %s is %d\n",p,register_index);
										instructions[target_instructions].target.base_index = register_index; 
										fflush(stdout);
										p += strlen(p)+1;
										register_index = get_register_index(p);
										AUDIT
										printf("register index for scale register %s is %d\n",p,register_index);
										instructions[target_instructions].target.scale_index = register_index; 
										p += strlen(p)+1;
										AUDIT
										printf("scale is %s\n",p);
										fflush(stdout);
										instructions[target_instructions].target.scale = (unsigned long)strtol(p,NULL,16);
										break; 
								}
							}
							else{
								AUDIT
								printf("no displacement found\n");
								fflush(stdout);
								p = aux = strtok(temp_line,"(,)");
								k = 0;
								do{
									aux = strtok(NULL,"(,)");//cont the arguments
									k++;
								}while(aux);

								switch(k){
									case 1: 
										register_index = get_register_index(p);
										AUDIT
										printf("register index for register %s is %d\n",p,register_index);
										fflush(stdout);
										instructions[target_instructions].target.base_index = register_index; 
										break; 
									case 2: 
										break; 
									case 3:
										register_index = get_register_index(p);
										AUDIT
										printf("register index for base register %s is %d\n",p,register_index);
										instructions[target_instructions].target.base_index = register_index; 
										fflush(stdout);
										p += strlen(p)+1;
										register_index = get_register_index(p);
										AUDIT
										printf("register index for scale register %s is %d\n",p,register_index);
										instructions[target_instructions].target.scale_index = register_index; 
										p += strlen(p)+1;
										AUDIT
										printf("scale is %s\n",p);
										fflush(stdout);
										instructions[target_instructions].target.scale = (unsigned long)strtol(p,NULL,16);
									       	break; 
								}

							}
#ifdef COMPUTE_ADDRESS
							strtok(temp_line,"(");
already_tokenized:
							p = strtok(NULL,"(");
							if (p[0] != ','){
								//we have the base register
								strtok(p,",)");
								register_index = get_register_index(p);
								AUDIT
								printf("register index for register %s is %d\n",p,register_index);
							}else{
								strtok(p,",)");

							}
#endif

							}
									
								/* Filtro di supporto backend-wide:
								 * - solo addressing modes che il runtime sa riscrivere
								 * - il detour vero e proprio viene costruito in finalize_detours()
								 */
								if (instructions[target_instructions].target.base_index <= 0) {
									continue;
								}
								if (instructions[target_instructions].target.scale_index != 0) {
									continue;
								}
								if (instructions[target_instructions].parsed_index < 0) {
									fail_unsupported_detour(&instructions[target_instructions], "missing parsed index for target instruction", instruction_start_address);
								}

								instructions[target_instructions].instrumentation_instructions = 0; 
								instructions[target_instructions].record_index = target_instructions; 


						target_instructions++;

					}	


				}
				break;

			}
		}

	}

	return target_instructions;

}

unsigned long find_elf_parse_compile_time_address(char * parsable_elf){

	FILE* the_file;
	char* guard;
	unsigned long function_start_address;
	long corrector;

	AUDIT
	printf("finding elf_parse compile time address\n");

	the_file = fopen(parsable_elf,"r");
	if(!the_file){
		printf("%s: disassembly file %s not accessible\n",VM_NAME,parsable_elf);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	while(1){
		guard = fgets(buffer,LINE_SIZE,the_file);
		if(guard == NULL){
		       	       break;
		}
		strtok(buffer,"\n");
		if(strstr(buffer,"elf_parse")){
				AUDIT
				printf("found line for function elf_parse\n");
				strtok(buffer," ");
				AUDIT
				printf("address of function elf_parse is %s\n",buffer);
				function_start_address = (unsigned long)strtol(buffer,NULL,16);
				AUDIT
				printf("numerical address of function elf_parse is %p\n",(void*)function_start_address);
				fclose(the_file);
				return function_start_address;
		}
	}				
	
	fclose(the_file);
	return 0x0;
}

void find_intermediate_zones(char * parsable_elf){

	FILE* the_file;
	char* guard;
	unsigned long function_start_address;
	long corrector;
	
	AUDIT
	printf("finding intermediate zones\n");

	the_file = fopen(parsable_elf,"r");
	if(!the_file){
		printf("%s: disassembly file %s not accessible\n",VM_NAME,parsable_elf);
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	while(1){
		guard = fgets(buffer,LINE_SIZE,the_file);
		if(guard == NULL){
		       	       break;
		}
		strtok(buffer,"\n");
		if(strstr(buffer,"<_wrap_main>:")) goto out;
		if(strstr(buffer,"\tcli")){
				AUDIT
				printf("found line with the cli instruction\n");
				strtok(buffer,":");
				AUDIT
				printf("compile time address of a cli instruction is %s\n",buffer);
				intermediate_zones[++intermediate_zones_index] = (//unsigned long
						uint64_t)strtol(buffer,NULL,16) + asl_randomization;
				AUDIT
				printf("runtime time address of the cli instruction is %p\n",(void*)intermediate_zones[intermediate_zones_index]);
		}
	}				
	
out:
	fclose(the_file);

}

int __real_main(int, char**);

int __wrap_main(int argc, char ** argv){

	int ret;
	int i;
	char *command;
	char target_functions_buf[LINE_SIZE];
	char *saveptr;

	target_instructions = 0;
	parsed_instructions_count = 0;
	direct_branch_refs_count = 0;

	setup_memory_access_rules();

	asl_randomization = (unsigned long)elf_parse;
	AUDIT
	printf("runtime address of elf_parse is %p\n",elf_parse);
	asl_randomization = (unsigned long)((long)asl_randomization - (long)find_elf_parse_compile_time_address(disassembly_file));
	AUDIT
	printf("asl randomization is set to the value %p\n",(void*)asl_randomization);
	fflush(stdout);

	strncpy(target_functions_buf, target_functions, sizeof(target_functions_buf) - 1);
	target_functions_buf[sizeof(target_functions_buf) - 1] = '\0';

	i=0;
	functions[i] = strtok_r(target_functions_buf,",", &saveptr);
	while(functions[i]){
		i++;
		functions[i] = strtok_r(NULL,",", &saveptr);
	}

	find_intermediate_zones(disassembly_file);

	ret = elf_parse(functions,disassembly_file);		
	finalize_detours();

	AUDIT {
		printf("found %d instructions to instrument\n",ret);
		fflush(stdout);
	}

	build_intermediate_representation();
	
	build_patches();

#ifdef APPLY_PATCHES
	apply_patches();
#endif

	AUDIT {
		printf("__mvm: list of instructions instrumented\n");
		for (i=0;i<ret;i++){
			audit_block(instructions+i);
		}
	}

	AUDIT
	printf("patches applied - control goes to the actual program\n");
	fflush(stdout);

	return __real_main(argc,argv);

}//this is the end of the MVM execution
