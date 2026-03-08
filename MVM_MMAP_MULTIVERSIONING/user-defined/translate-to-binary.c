#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_INSTRUCTION_SIZE 256  // Maximum length for input instruction

int main() {
    char input[MAX_INSTRUCTION_SIZE];  // Buffer to hold user input string
    unsigned char instruction[MAX_INSTRUCTION_SIZE / 3];  // Array to store instruction bytes
    int byteCount = 0;
    unsigned int value;

    // Get user input - a sequence of hexadecimal characters (e.g., '89 90 00 00 20 00')
    fgets(input, sizeof(input), stdin);

    // Remove newline character if present
    input[strcspn(input, "\n")] = 0;

    // Parse the input string and convert hexadecimal bytes into actual binary values
    char *ptr = input;
    while (sscanf(ptr, "%2x", &value) == 1) {  // Read two characters as a hex byte
        instruction[byteCount++] = (unsigned char)value;  // Store byte into the instruction array
        ptr += 3;  // Move the pointer by 3 to skip over the two hex characters and space
    }

    // Output the result - the raw instruction in binary (hex format)
    for (int i = 0; i < byteCount; i++) {
	write(1,&instruction[i],1);
    }

    return 0;
}

