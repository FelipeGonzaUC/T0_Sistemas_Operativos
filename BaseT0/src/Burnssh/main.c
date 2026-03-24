
#include <stdio.h>
#include <stdlib.h>
#include "../input_manager/manager.h"
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define TRUE 1
#define FALSE 0

typedef struct process
{
  pid_t pid;
  char* name;
  time_t start_time;
  int execution_time;
  short running;
  short exit_code;
  short signal_val;
  struct process* next;
  int limit_time;
} Process;

typedef struct process_list
{
  Process* head;
  Process* tail;
  int size;
  int shutdown;
} ProcessList;

void initList(ProcessList* list)
{
  list->head = NULL;
  list->tail = NULL;
  list->size = 0;
  list->shutdown = FALSE;
}

void initProcess(Process* process, pid_t pid, char* name)
{
  process->pid = pid;
  process->name = name;
  process->start_time = time(NULL);
  process->execution_time = 0;
  process->running = TRUE;
  process->exit_code = -1;
  process->signal_val = -1;
  process->next = NULL;
  process->limit_time = -1;
}

void insertProcess(ProcessList* list, Process* process)
{
  if (list->size == 0)
  {
    list->head = process;
    list->tail = process;
  }
  else
  {
    list->tail->next = process;
    list->tail = process;
  }
  list->size++;
}

void proccesInfo(Process* process)
{
  printf("PID: %d\n", process->pid);
  printf("Name: %s\n", process->name);
  printf("Execution Time: %d seconds\n", process->execution_time);
  printf("Paused: %s\n", process->running ? "FALSE" : "TRUE");
  printf("Exit Code: %d\n", process->exit_code);
  printf("Signal Value: %d\n", process->signal_val);
}

Process* findProcessByPID(ProcessList* list, pid_t pid)
{
  Process* current = list->head;
  while (current != NULL)
  {
    if (current->pid == pid)
    {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

void status(ProcessList* list)
{
  Process* current = list->head;
  while (current != NULL)
  {
    proccesInfo(current);
    current = current->next;
  }
}

void freeListProcesses(ProcessList* list)
{
  Process* current = list->head;
  while (current != NULL)
  {
    Process* temp = current;
    current = current->next;
    free(temp);
  }
  list->head = NULL;
  list->tail = NULL;
  list->size = 0;
}

//Global variables 
ProcessList* process_history;
Process* current_processes[10] = {NULL};
int system_shutdown = FALSE;


//Funciones para manejo de procesos concurrentes
void timeout_process()
{
  int status;

  while (TRUE)
  {
    while (TRUE)
    {
      pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
      if (pid <= 0) break;

      Process* process = findProcessByPID(process_history, pid);
      if (process == NULL) continue;

      if (WIFEXITED(status))
      {
        process->exit_code = WEXITSTATUS(status);
        process->running = FALSE;
      }
      else if (WIFSIGNALED(status))
      {
        process->signal_val = WTERMSIG(status);
        process->running = FALSE;
      }
    }

    time_t current_time = time(NULL);
    Process* current = process_history->head;
    while (current != NULL)
    {
      if (current->running && current->limit_time > 0)
      {
        current->execution_time = (int)difftime(current_time, current->start_time);
        if (current->execution_time >= current->limit_time)
        {
          kill(current->pid, SIGTERM);
          sleep(5);
          if (current->running) kill(current->pid, SIGKILL);
        }
      }
      current = current->next;
    }
  }
}

//Funciones handler de signals
void alarm_handler(int signum)
{
  if (process_history->shutdown)
  {
    printf("El sistema está en proceso de apagado. No se pueden abortar procesos.\n");
    return;
  }
  for (int i = 0; i < 10; i++)
  {
    if (current_processes[i]->running)
    {
      printf("Abort cumplido.\n%d %s %d %d %d %d", current_processes[i]->pid, current_processes[i]->name, current_processes[i]->execution_time, current_processes[i]->running ? 0 : 1, current_processes[i]->exit_code, current_processes[i]->signal_val);
      kill(current_processes[i]->pid, SIGTERM);
    }
  }
}

void termination_handler(int signum)
{
  //Este handler se activa cuando se ejecuta shutdown
  system_shutdown = TRUE;

  Process* current = process_history->head;
  while (current != NULL)
  {
    if (current->running) kill(current->pid, SIGKILL);
    current = current->next;
  }
}

int main(int argc, char const *argv[])
{
  process_history = (ProcessList*)malloc(sizeof(ProcessList));
  initList(process_history);
  pid_t timeout_pid = fork();
  if (timeout_pid == 0)
  {
    timeout_process();
    exit(0);
  }
  set_buffer(); // No borrar

  while (TRUE)
  {
    //Reminder: activate in terminate process
    if (system_shutdown) break;
    char** input = read_user_input();
    printf("%s\n", input[0]);
    if (strcmp(input[0], "launch") == 0)
    {
      pid_t pid = fork();
      if (pid == 0)
      {
        //Proceso hijo
      }
      else if (pid > 0)
      {
        //Proceso Padre
        Process* new_process = (Process*)malloc(sizeof(Process));
        initProcess(new_process, pid, input[1]);
        if (argc > 1)
        {
          new_process->limit_time = atoi(input[2]);
        }
        else
        {
          new_process->limit_time = -1;
        }
      }
    }
    else if (strcmp(input[0], "status") == 0)
    {
      status(process_history);
    }
    else if (strcmp(input[0], "abort") == 0)
    {
      if (!process_history->shutdown)
      {
        int seconds = atoi(input[1]);
        int active_processes = 0;
        Process* current = process_history->head;
        while (current != NULL)
        {
          if (current->running && active_processes < 10) {
            current_processes[active_processes] = current;
            active_processes++;
          }
          current = current->next;
        }
        if (active_processes == 0)
        {
          printf("No hay procesos activos para abortar.\n");
          for (int i = 0; i < 10; i++) {
            current_processes[i] = NULL;
          }
        }
        else
        {
          signal(SIGALRM, alarm_handler);
          alarm(seconds);
        }
      }
      else
      {
        printf("El sistema está en proceso de apagado. No se pueden abortar procesos.\n");
      }
    }
    else if (strcmp(input[0], "pause") == 0)
    {
      pid_t pid = atoi(input[1]);
      Process* process = findProcessByPID(process_history, pid);
      if (process != NULL && process->running)
      {
        kill(pid, SIGSTOP);
        process->running = FALSE;
        time_t current_time = time(NULL);
        process->execution_time = (int)difftime(current_time, process->start_time);
      }
      else
      {
        printf("Proceso con PID %d no encontrado o ya está pausado.\n", pid);
      }
    }
    else if (strcmp(input[0], "resume") == 0)
    {
      pid_t pid = atoi(input[1]);
      Process* process = findProcessByPID(process_history, pid);
      if (process != NULL && !process->running)
      {
        kill(pid, SIGCONT);
        process->running = TRUE;
        time_t current_time = time(NULL);
        process->execution_time = (int)difftime(current_time, process->start_time);
      }
      else
      {
        printf("Proceso con PID %d no encontrado o ya está en ejecución.\n", pid);
      }
    }
    else if (strcmp(input[0], "shutdown") == 0)
    {
      process_history->shutdown = TRUE;
      Process* current = process_history->head;
      while (current != NULL)
      {
        if (current->running) break;
        current = current->next;
      }
      if (current == NULL)
      {
        printf("No hay procesos activos. Apagando el sistema...\n");
      }
      else
      {
        printf("Apagando el sistema. Se abortarán todos los procesos activos.\n");
        for (int i = 0; i < 10; i++)
        {
          if (current_processes[i] != NULL && current_processes[i]->running)
          {
            kill(current_processes[i]->pid, SIGINT);
            signal(SIGALRM, termination_handler);
            alarm(10);
          }
        } 
      }
    }
    free_user_input(input);
  }
  kill(timeout_pid, SIGKILL);
  freeListProcesses(process_history);
  free(process_history);
}

