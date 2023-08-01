#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <signal.h>

//global variables
int allow_back = 1;
pid_t back_proc[100] = {0};

//command struct
struct command {
    char **argv;
    int argc;
    char *in;
    char *out;
    int flag;
};

/*
 * This function should handle the SIGTSTP signal and enter or exit foreground mode.
 *
 * Params:
 *   signo - the terminating signal
 */
void handle_SIGSTP(int signo) {
    //print out message if entering foreground mode
    if(allow_back == 1) {
        write(STDOUT_FILENO, "Entering foreground-only mode (& is now ignored)\n: ", 52);
        fflush(stdout);
        allow_back = 0;
    } else {
        //print out exiting foreground 
        write(STDOUT_FILENO, "Exiting foreground-only mode\n: ", 32);
        fflush(stdout);
        allow_back = 1;
    }
}

/*
 * This function should print out the exit status of the last foreground proccess
 *
 * Params:
 *   status - the exit status of the last foreground proccess.
 */
void print_status(int status) {
    //if the child exited normally
    if(WIFEXITED(status)) { 
        printf("Exit value %d\n", WEXITSTATUS(status));
        fflush(stdout);
    } else {
        //if the child was terminated by a signal
        printf("terminated by signal %d\n", WTERMSIG(status));
        fflush(stdout);
    }
}

/*
 * This function should replace all instances of && in an input string with 
 * the proccess id of small shell.
 * Adapted from https://www.geeksforgeeks.org/c-program-replace-word-text-another-given-word/
 *
 * Params:
 *   s - the original string.
 *   old - "&&" the string to replace.
 *   new - the proccess id in string form.
 */
char *expand_variables(const char *s, const char *old, const char *new) {
    //declare needed variables
    char *result;
    int i, cnt = 0;
    int new_len = strlen(new);
    int old_len = strlen(old);

    //count number of times old appears in the string
    for(i = 0; s[i] != '\0'; i++) {
        if(strstr(&s[i], old) == &s[i]) {
            cnt++;
            i += old_len - 1;
        }
    }

    //allocate a new string
    result = malloc(i + cnt *(new_len - old_len) + 1);

    i = 0;
    while(*s) {
        //compare substring with result
        if(strstr(s, old) == s) {
            strcpy(&result[i], new);
            i += new_len;
            s += old_len;
        } else {
            result[i++] = *s++;
        }
    }

    //add null terminator and return
    result[i] = '\0';
    return result;
}

/*
 * This function should parse over a an input string and break it down
 * into the command, arguments, input file, output file, and background mode.
 *
 * Params:
 *   none.
 */
struct command *get_command() {
    //prompt for command
    write(STDOUT_FILENO, ": ", 2);
    fflush(stdout);

    //declare variables for buffering
    int i;
    char *saveptr;
    char *bf = NULL;
    size_t len = 0;
    ssize_t line;
    
    //allocate struct attributes
    struct command *cmd = malloc(sizeof(struct command));
    cmd->argv = calloc(520, sizeof(char *));
    cmd->argc = 0;
    cmd->in = NULL;
    cmd->out = NULL;
    cmd->flag = 0;

    //get command
    line = getline(&bf, &len, stdin);

    //copy string over to new string
    char input[1024];
    char id_str[1024];
    char c[] = "$$";
    int id = getpid();
    for(i = 0; i < strlen(bf) + 1; i++) {
        input[i] = bf[i];
    }
    input[strlen(input)] = '\0';
    sprintf(id_str, "%d", id);
  
    //if string contains && expand them
    if(strstr(input, c) != NULL) {
        char *result = expand_variables(input, c, id_str);
        free(bf);
        bf = result;
    }

    //clear stream if getline fails
    if(line == -1) {
        clearerr(stdin);
    } else {
        //parse over string and get commands
        char *token = strtok_r(bf, " \n", &saveptr);
        if(token == NULL || token[0] == '#') {
            free(bf);
            return NULL;
        }

        do {
            //allocate memory for strings and copy over
            cmd->argv[cmd->argc] = calloc(strlen(token) + 1, sizeof(char));
            strcpy(cmd->argv[cmd->argc], token);
            cmd->argc++;
        } while((token = strtok_r(NULL, " \n", &saveptr)) != NULL);

        //set background flag if & is detected
        if(strcmp(cmd->argv[cmd->argc - 1], "&") == 0) {
            cmd->flag = 1;
            free(cmd->argv[cmd->argc -1]);
            cmd->argv[cmd->argc -1] = NULL;
            cmd->argc--;
        }

        //check for input and output redirection
        for(i = 0; i < 2; i++) if(cmd->argc >= 2) {
            if(strcmp(cmd->argv[cmd->argc - 2], ">") == 0 && cmd->out == NULL) {
                cmd->out = cmd->argv[cmd->argc - 1];
                free(cmd->argv[cmd->argc - 2]);
                cmd->argv[cmd->argc - 2] = NULL;
                cmd->argv[cmd->argc - 1] = NULL;
                cmd->argc -= 2;
            } else if(strcmp(cmd->argv[cmd->argc - 2], "<") == 0 && cmd->in == NULL) {
                cmd->in = cmd->argv[cmd->argc - 1];
                free(cmd->argv[cmd->argc - 2]);
                cmd->argv[cmd->argc - 2] = NULL;
                cmd->argv[cmd->argc - 1] = NULL;
                cmd->argc -= 2;
            }
        }

        free(bf);
        return cmd;
    }
}

/*
 * This function should terminate all proccesses then exit small shell.
 *
 * Params:
 *   none.
 */
void exit_shell() {
    //loop through array and terminate processes
    int i;
    for(i = 0; i < 99; i++) {
        if(back_proc[i] > 0) {
            kill(back_proc[i], SIGKILL);
        }
    }
    exit(1);
}

/*
 * This function should change the current directory
 *
 * Params:
 *   path - the path to the directory, can be absolute or relative.
 */
void cd(char *path) {
    //get the HOME env path
    char *home = getenv("HOME");
    //if there isn't a path, got to root
    if(path == NULL) {
        chdir(home);
    } else {
        //cd into home then follow relative path
        if(path[0] == '~') {
            chdir(home);
            path[0] == '.';
            chdir(path);
        } else {
            chdir(path);
        }
    }
}

/*
 * This function should change the input or output of a command location.
 * uses /dev/null if in background mode
 *
 * Params:
 *   cmd - the command struct.
 */
void redirect(struct command *cmd) {\
    //background command
    if(cmd->flag == 1) {
        //open input file if permitable
        if(cmd->in != NULL) {
            int source_fd = open(cmd->in, O_RDONLY);
            if(source_fd == -1) { 
                perror("source open()"); 
                exit(1); 
            }
            int result = dup2(source_fd, 0);
            if(result == -1) { 
            perror("source dup2()"); 
            exit(1); 
            }
        //open /dev/null otherwise
        } else {
            int source_fd = open("/dev/null", O_RDONLY);
            if(source_fd == -1) { 
                perror("source open()"); 
                exit(1); 
            }
            int result = dup2(source_fd, 0);
            if(result == -1) { 
            perror("source dup2()"); 
            exit(1);
            }
        }

        //open output file if permitable
        if(cmd->out != NULL) {
            int target_fd = open(cmd->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(target_fd == -1) { 
                perror("target open()"); 
                exit(1); 
            }   
            int result = dup2(target_fd, 1);
            if(result == -1) { 
                perror("target dup2()"); 
                exit(1); 
            }
        //open /dev/null otherwise
        } else {
            int target_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(target_fd == -1) { 
                perror("target open()"); 
                exit(1); 
            }   
            int result = dup2(target_fd, 1);
            if(result == -1) { 
                perror("target dup2()"); 
                exit(1); 
            }
        }
    //if in foreground mode
    } else {
        //open input file if permitable
         if(cmd->in != NULL) {
            int source_fd = open(cmd->in, O_RDONLY);
            if(source_fd == -1) { 
                perror("source open()"); 
                exit(1); 
            }
            int result = dup2(source_fd, 0);
            if(result == -1) { 
            perror("source dup2()"); 
            exit(1); 
            }
        }

        //open output file if permitable
        if(cmd->out != NULL) {
            int target_fd = open(cmd->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(target_fd == -1) { 
                perror("target open()"); 
                exit(1); 
            }   
            int result = dup2(target_fd, 1);
            if(result == -1) { 
                perror("target dup2()"); 
                exit(1); 
            }
        }
    }
}

/*
 * This function should execute a non-built in function by searching the PATH variable.
 *
 * Params:
 *   cmd - the command struct.
 *   exit_status - the exit status of the last foreground command.
 *   ign_SIGINT - the sigaction struct.
 */
void other(struct command *cmd, int *exit_status, struct sigaction ign_SIGINT) {
    //fork a child proccess
    pid_t spawn_pid = fork();

    switch(spawn_pid) {
        //if the fork fails print error
        case -1:
            perror("fork()\n");
            exit(1);
            break;
        //succssefull fork, child proccess
        case 0:;
            //child processes should ignore SIGTSTP
            struct sigaction ign_SIGSTP = {0};
            ign_SIGSTP.sa_handler = SIG_IGN;
            sigfillset(&ign_SIGSTP.sa_mask);
            ign_SIGSTP.sa_flags = 0;
            sigaction(SIGTSTP, &ign_SIGSTP, NULL);

            //child proccess should ignore SIGINT if in background
            if(cmd->flag == 0) {
                ign_SIGINT.sa_handler = SIG_DFL;
                sigaction(SIGINT, &ign_SIGINT, NULL);
            }

            //rederect input/output if needed
            redirect(cmd);
            //execute command via PATH
            execvp(cmd->argv[0], cmd->argv);
            perror("execve");
            exit(2);
            break;
        default:
            //if a child proccess is in background and background is enabled
            if(cmd->flag == 1 && allow_back == 1) {
                //add pid to array
                int i;
                for(i = 0; i < 99; i++) {
                    if(back_proc[i] == 0) {
                        back_proc[i] = spawn_pid;
                        break;
                    }
                }
                //don't wait for background proccess to finish
                pid_t parent = waitpid(spawn_pid, exit_status, WNOHANG);
                printf("background pid is %d\n", spawn_pid);
                fflush(stdout);
            } else {
                //wait for foreground proccess and print termination status
                pid_t parent = waitpid(spawn_pid, exit_status, 0);
                if(WIFSIGNALED(*exit_status)) {
                    printf("terminated by signal %d\n", WTERMSIG(*exit_status));
                    fflush(stdout);
                }
            }
            break;    
        }

    //check for finished background ids
    //adapted from https://stackoverflow.com/questions/5278582/checking-the-status-of-a-child-process-in-c
    while((spawn_pid = waitpid(-1, exit_status, WNOHANG)) > 0) {
        //print id if in foreground mode
		if(allow_back == 1) {
            printf("background pid %d is done\n", spawn_pid);
		    print_status(*exit_status);
		    fflush(stdout);
        }

        //remove pid from array
        int i;
        for(i = 0; i < 99; i++) {
            if(back_proc[i] == spawn_pid) {
                back_proc[i] = 0;
                break;
            }
        }
	}
}

/*
 * This function should run small shell, get commands, and run the proper function.
 *
 * Params:
 *   none.
 */
int main() {
    //exit status of last foreground command
    int exit_status = 0;

    //ctrl z signal handler
    struct sigaction ign_SIGSTP = {0};
    ign_SIGSTP.sa_handler = handle_SIGSTP;
    sigfillset(&ign_SIGSTP.sa_mask);
    ign_SIGSTP.sa_flags = 0;
    sigaction(SIGTSTP, &ign_SIGSTP, NULL);

    //ctrl c signal handler
    struct sigaction ign_SIDINT = {0};
    ign_SIDINT.sa_handler = SIG_IGN;
    sigfillset(&ign_SIDINT.sa_mask);
    ign_SIDINT.sa_flags = 0;
    sigaction(SIGINT, &ign_SIDINT, NULL);

    while(1) {
        //get the command 
        struct command *cmd = get_command();

        //if command is blank or #
        if(cmd == NULL) {
            continue;
        } else {
            //if the command is exit, exit the shell
            if(strcmp(cmd->argv[0], "exit") == 0) {
                exit_shell();
            //if the command is cd, run built in cd
            } else if(strcmp(cmd->argv[0], "cd") == 0) {
                cd(cmd->argv[1]);
            //if the command is status, run built in status
            } else if(strcmp(cmd->argv[0], "status") == 0) {
                print_status(exit_status);
            //run any other command 
            } else {
                other(cmd, &exit_status, ign_SIDINT);
            }

            //free memory
            free(cmd->argv);
            free(cmd);
        }
    }
    exit(EXIT_SUCCESS);
}