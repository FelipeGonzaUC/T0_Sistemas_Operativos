#include <stdio.h>
#include <stdlib.h>
#include "../input_manager/manager.h"
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>

#define TRUE 1
#define FALSE 0

typedef struct process
{
  pid_t pid;
  char* name;
  time_t start_time;
  int accumulated_time;
  int execution_time;
  short running;
  short stopped;
  short exit_code;
  short signal_val;
  struct process* next;
  int limit_time;
  short term_sent;
  time_t term_sent_time;
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
    process->accumulated_time = 0;
  process->execution_time = 0;
  process->running = TRUE;
  process->stopped = FALSE;
  process->exit_code = -1;
  process->signal_val = -1;
  process->next = NULL;
  process->limit_time = -1;
  process->term_sent = FALSE;
  process->term_sent_time = 0;
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
    if (current->pid == pid) return current;
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
    free(current->name);
    current = current->next;
    free(temp);
  }
  list->head = NULL;
  list->tail = NULL;
  list->size = 0;
}

//other functions

//Global variables
ProcessList* process_history;
Process* current_processes[10] = {NULL};
int system_shutdown = FALSE;
static pthread_mutex_t process_history_mutex = PTHREAD_MUTEX_INITIALIZER;
static int default_limit_time = -1;

//Thread que maneja signals y timeout
static void* timeout_thread(void* arg)
{
    (void)arg;
    int status;

    while (!system_shutdown)
    {
        while (1)
        {
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid <= 0) break;
            //lock resourses
            pthread_mutex_lock(&process_history_mutex);
            Process* process = findProcessByPID(process_history, pid);
            if (process)
            {
                time_t now = time(NULL);
                if (process->running)
                {
                    int total = process->accumulated_time + (int)difftime(now, process->start_time);
                    process->execution_time = total;
                    process->accumulated_time = total;
                }

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
            pthread_mutex_unlock(&process_history_mutex);
        }

        time_t now = time(NULL);
        pthread_mutex_lock(&process_history_mutex);
        for (Process* p = process_history->head; p != NULL; p = p->next)
        {
            if (!p->running) continue;

            int elapsed = p->accumulated_time + (int)difftime(now, p->start_time);
            p->execution_time = elapsed;

            // no timeout
            if (p->limit_time <= 0) continue;

            if (elapsed >= p->limit_time)
            {
                if (!p->term_sent)
                {
                    kill(p->pid, SIGTERM);
                    p->term_sent = TRUE;
                    p->term_sent_time = now;
                }
                else
                {
                    if ((int)difftime(now, p->term_sent_time) >= 5)
                    {
                        kill(p->pid, SIGKILL);
                    }
                }
            }
        }
        pthread_mutex_unlock(&process_history_mutex);
        //Sleep para no consumir CPU 
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000}; // 100ms
        nanosleep(&ts, NULL);
    }

    return NULL;
}

//Signal handler para abort y terminate
void alarm_handler(int sigsum)
{
    (void)sigsum;
    pthread_mutex_lock(&process_history_mutex);
    if (process_history->shutdown)
    {
        pthread_mutex_unlock(&process_history_mutex);
        printf("El sistema está en proceso de apagado. No se pueden abortar procesos.\n");
        return;
    }
    for (int i = 0; i < 10; i++)
    {
        if (current_processes[i] != NULL && current_processes[i]->running)
        {
            printf("Abort cumplido.\n%d %s %d %d %d %d\n", current_processes[i]->pid, current_processes[i]->name, current_processes[i]->execution_time, current_processes[i]->running ? 0 : 1, current_processes[i]->exit_code, current_processes[i]->signal_val);
            kill(current_processes[i]->pid, SIGTERM);
        }
    }
    pthread_mutex_unlock(&process_history_mutex);
}

void termination_handler(int signum)
{
    (void)signum;
    system_shutdown = TRUE;
    pthread_mutex_lock(&process_history_mutex);
    Process* current = process_history->head;
    while (current != NULL)
    {
        if (current->running) kill(current->pid, SIGKILL);
        current = current->next;
    }
    pthread_mutex_unlock(&process_history_mutex);
}

//Main
int main(int argc, char const *argv[])
{
    process_history = (ProcessList*)malloc(sizeof(ProcessList));
    initList(process_history);
    if (argc > 1) default_limit_time = atoi(argv[1]);
    else default_limit_time = -1;
    pthread_t watchdog;
    pthread_create(&watchdog, NULL, timeout_thread, NULL);
    pthread_detach(watchdog); // or join on shutdown
    set_buffer(); // No borrar

    while (TRUE)
    {
        if (system_shutdown) break;
        char** input = read_user_input();
        printf("%s\n", input[0]);
        if (strcmp(input[0], "launch") == 0)
        {
            int pipefd[2];
            pipe(pipefd);
            int flags = fcntl(pipefd[1], F_GETFD);
            if (flags == -1 || fcntl(pipefd[1], F_SETFD, flags| FD_CLOEXEC) == -1) 
            {
                perror("Launch faild");
            }
            pid_t pid = fork();
            if (pid == 0)
            {
                close(pipefd[0]);
                execvp(input[1], &input[1]);
                int err = errno;
                (void)write(pipefd[1], &err, sizeof(err));
                _exit(127);
            }
            else if (pid > 0)
            {
                close(pipefd[1]);
                int err = 0;
                ssize_t n = read(pipefd[0], &err, sizeof(err));
                close(pipefd[0]);
                if(n == 0)
                {
                    printf("Process started correctly\n");
                    Process* new_process = (Process*)malloc(sizeof(Process));
                    char* name = strdup(input[1]);
                    initProcess(new_process, pid, name);
                    if (argc > 2)
                    {
                        new_process->limit_time = atoi(input[2]);
                    }
                    else
                    {
                        new_process->limit_time = default_limit_time;
                    }
                    pthread_mutex_lock(&process_history_mutex);
                    insertProcess(process_history, new_process);
                    pthread_mutex_unlock(&process_history_mutex);
                }
                else
                {
                    printf("Failed to execute: file not found");
                }
            }
        }
        else if (strcmp(input[0], "status") == 0)
        {
            pthread_mutex_lock(&process_history_mutex);
            status(process_history);
            pthread_mutex_unlock(&process_history_mutex);
        }
        else if (strcmp(input[0], "abort") == 0)
        {
            pthread_mutex_lock(&process_history_mutex);
            int is_shutting_down = process_history->shutdown;
            pthread_mutex_unlock(&process_history_mutex);

            if (!is_shutting_down)
            {
                int seconds = atoi(input[1]);
                int active_processes = 0;

                pthread_mutex_lock(&process_history_mutex);
                Process* current = process_history->head;
                while (current != NULL)
                {
                    if (current->running && active_processes < 10)
                    {
                        current_processes[active_processes] = current;
                        active_processes++;
                    }
                    current = current->next;
                }
                                pthread_mutex_unlock(&process_history_mutex);

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
        }
        else if (strcmp(input[0], "pause") == 0)
        {
            pid_t pid = atoi(input[1]);

            pthread_mutex_lock(&process_history_mutex);
            Process* process = findProcessByPID(process_history, pid);
            if (process != NULL && process->running)
            {
                kill(pid, SIGSTOP);
                time_t current_time = time(NULL);
                process->accumulated_time += (int)difftime(current_time, process->start_time);
                process->execution_time = process->accumulated_time;
                process->running = FALSE;
                process->stopped = TRUE;
            }
            else
            {
                printf("Proceso con PID %d no encontrado o ya está pausado.\n", pid);
            }
            pthread_mutex_unlock(&process_history_mutex);
        }
        else if (strcmp(input[0], "resume") == 0)
        {
            pid_t pid = atoi(input[1]);

            pthread_mutex_lock(&process_history_mutex);
            Process* process = findProcessByPID(process_history, pid);
            if (process != NULL && !process->running)
            {
                kill(pid, SIGCONT);
                process->running = TRUE;
                process->stopped = FALSE;
                process->start_time = time(NULL);
            }
            else
            {
                printf("Proceso con PID %d no encontrado o ya está en ejecución.\n", pid);
            }
            pthread_mutex_unlock(&process_history_mutex);
        }
        else if (strcmp(input[0], "shutdown") == 0)
        {
            pthread_mutex_lock(&process_history_mutex);
            process_history->shutdown = TRUE;
            Process* current = process_history->head;
            while (current != NULL)
            {
                if (current->running) kill(current->pid, SIGKILL);
                current = current->next;
            }
            pthread_mutex_unlock(&process_history_mutex);

            if (current == NULL)
            {
                printf("No hay procesos activos para apagar.\n");
            }
            else
            {
                printf("Sistema apagado. Todos los procesos han sido terminados.\n");
                for (int i = 0; i < 10; i++)
                {
                    if (current_processes[i] != NULL && current_processes[i]->running)
                    {
                        kill(current_processes[i]->pid, SIGKILL);
                        signal(SIGALRM, termination_handler);
                        alarm(10);
                    }
                }
            }
        }
        free_user_input(input);
    }
    printf("Burnssh finalizado.\n");
    status(process_history);
    freeListProcesses(process_history);
    free(process_history);
    return 0;
}