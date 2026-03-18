
#include <stdio.h>
#include <stdlib.h>
#include "../input_manager/manager.h"

int main(int argc, char const *argv[])
{
  set_buffer(); // No borrar

  char** input = read_user_input();
  printf("%s\n", input[0]);
  free_user_input(input);
}

