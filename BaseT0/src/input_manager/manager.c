// Import used global libraries
#include <stdio.h>  // FILE, fopen, fclose, etc.
#include <string.h> // strtok, strcpy, etc.
#include <stdlib.h> // malloc, calloc, free, etc.

// Import the header file of this module, because it has the constant definitions
#include "manager.h"

static char inbuf[BUFFER_SIZE];

/*
  * Sets the buffer for stdin to a custom size defined by BUFFER_SIZE.
*/
void set_buffer(void) {
  setvbuf(stdin, inbuf, _IOFBF, sizeof inbuf);
}

/*
 * Splits a string "str" by a separator "sep", returns an array with the
 * resulting strings. Equivalent to Python's str.split(sep).
 */
static char **split_by_sep(char *str, char *sep)
{
  char **new_str = calloc(MAX_SPLIT, sizeof(char *));
  int index = 0, len;

  char *token = strtok(str, sep);
  while (token != NULL)
  {
    new_str[index] = calloc(BUFFER_SIZE, sizeof(char));
    strcpy(new_str[index++], token);
    token = strtok(NULL, sep);
  }

  // Remove dangling Windows (\r) and Unix (\n) newlines
  len = strlen(new_str[index - 1]);
  if (len > 1 && new_str[index - 1][len - 2] == '\r')
    new_str[index - 1][len - 2] = '\0';
  else if (len && new_str[index - 1][len - 1] == '\n')
    new_str[index - 1][len - 1] = '\0';
  return new_str;
}

/*
 * Reads a line fo user input and returns it as an array of strings
 */
char **read_user_input()
{
  char input[BUFFER_SIZE];
  fgets(input, BUFFER_SIZE, stdin);
  char **split_input = split_by_sep(input, " ");
  return split_input;
}

/*
 * Frees user input obtained by the read_user_input function
 */
void free_user_input(char **input)
{
  for (int i = 0; i < MAX_SPLIT; i++)
  {
    free(input[i]);
  }
  free(input);
}
