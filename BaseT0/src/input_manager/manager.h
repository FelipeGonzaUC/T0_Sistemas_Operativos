// Tells the compiler to compile this file once
#pragma once

// Define compile-time constants
#define MAX_SPLIT 255
#define BUFFER_SIZE 1024

// Declare functions
char **read_user_input();
void set_buffer(void);
void free_user_input(char **input);