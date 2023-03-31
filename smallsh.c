#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

//Function prototypes for use in main.
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
char *home_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
void free_array(char **wordArr, int array_size);
void handle_SIGINT(int signo){};

//Global variables for $? and $!
int dol_ques = 0;
pid_t dol_exc;


int main(int argc, char *argv[]) {

  //Function only takes one argument
  if (argc != 1) {
    printf("Invalid arguments input");
    exit (1);
  }

step1:;

   char *inputPtr = NULL;
   size_t n = 0;

for (;;) {

  //Ignore SIGINT and SIGTSTP
  struct sigaction ignore_action = {0}, SIGINT_action = {0};
  ignore_action.sa_handler = SIG_IGN;
  sigaction(SIGINT, &ignore_action, NULL);
  sigaction(SIGTSTP, &ignore_action, NULL);

  //Check for any un-waited-for background processes in the same process group ID as smallsh.
  pid_t child_id;
  int status;
  while ((child_id = waitpid(0, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    if (WIFEXITED(status)) fprintf(stderr, "Child process %d done. Exit status %d\n", child_id, WEXITSTATUS(status));
    if (WIFSIGNALED(status)) fprintf(stderr, "Child process %d done. Signaled %d.\n", child_id, WTERMSIG(status));
    if (WIFSTOPPED(status)) {
      kill(child_id, SIGCONT);
      fprintf(stderr, "Child process %d stopped. Continuing.\n", child_id);
    }
  }

  //INPUT
  //Display Prompt:
  char *displayPrompt = getenv("PS1");
  if (displayPrompt == NULL) displayPrompt = "";

  if(fprintf(stderr, "%s", displayPrompt) < 0) {
    perror("Issue displaying PS1:");
  };
  
  //Get string from user:

  //Set handler for SIGINT to do nothing.
  SIGINT_action.sa_handler = handle_SIGINT;
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;
  sigaction(SIGINT, &SIGINT_action, NULL);

  ssize_t line_length = getline(&inputPtr, &n, stdin);
  if (feof(stdin)){
    fprintf(stderr, "\nexit\n");
    exit(dol_ques);
  }
  if (line_length == -1){
    clearerr(stdin);
    errno = 0;
    printf("\n");
    goto step1;
  }

  //Set handler for SIGINT to be ignored.
  sigaction(SIGINT, &ignore_action, NULL);

  //Split string into array of words.
  char *wordArr[512];
  int i = 0, array_size;
  char *delim = getenv("IFS");
  if (delim == NULL) delim = " \t\n";

  //Tokenize getline input and store in wordArr. 
  char *token = strtok(inputPtr, delim);
  if (token == NULL) {
    goto step1;
  }

  for (;;) {
    char *wordDup = strdup(token);
    wordArr[i] = malloc(sizeof token);
    wordArr[i] = wordDup;
    i++;
    token = strtok(NULL, delim);
    if (token == NULL) break;
  }
  
  //Loop through word array and expand words based on parameters
  pid_t pid = getpid();
  char pid_str[20];
  char dol_ques_str[10], dol_exc_str[10];
  if ((sprintf(pid_str, "%d", pid)) < 0) perror("Error in pid to string conversion: "); //Convert pid to string
  if ((sprintf(dol_ques_str, "%d", dol_ques)) < 0) perror("Error in $? to string: ");   //Convert status to string
  if (!dol_exc) {
    dol_exc_str[0] = '\0';
  } else {
    if ((sprintf(dol_exc_str, "%d", dol_exc)) < 0) perror("Error in $! to string: ");
  }
  for (int j = 0; j < i; j++) {
    wordArr[j] = str_gsub(&wordArr[j], "$$", pid_str);
    wordArr[j] = str_gsub(&wordArr[j], "$?", dol_ques_str);
    wordArr[j] = str_gsub(&wordArr[j], "$!", dol_exc_str);
    if ((strncmp("~/", wordArr[j], 2) == 0)) {
      wordArr[j] = home_gsub(&wordArr[j], "~", getenv("HOME"));      
    }
  }
  array_size = i;
  //Parse words into tokens based on specified parameters.
  int bgd = 0;
  for (int j = 0; j < i; j++) {
    if (strcmp(wordArr[j], "#") == 0) i = j;
  }
  if (i == 0) goto exit; //First argument is #, go back to step1
  if (strcmp(wordArr[i - 1], "&") == 0) { //If background, set bgd variable and decrement array position
    bgd = 1;
    i--;
  }

  int red_end = i;    //Placeholder for where redirection ends
  //Loop through to see if < or > present at end of array. Make i equal to end of arguments/beginning of redirection
  int j = 0;
  while ((i >= 2) && (j < i)) {
    if ((strcmp(wordArr[i-2], "<") == 0) || (strcmp(wordArr[i-2], ">") == 0)) {
      i = i - 2;
    }
    j++;
  }
  
  int arg_end = i; //Placeholder for when arguments end and redirection starts

  if (arg_end == 0) goto exit; //No command entered return to step 1.

// Execution of commands/arguments

  //Execute EXIT command.
  if (strcmp(wordArr[0], "exit") == 0) {
    int not_int = 0;
    if (arg_end == 2) {
      for (int i = 0 ; i < strlen(wordArr[1]); i++) {
        if (isdigit(wordArr[1][i]) == 0) not_int = 1;
      }
    } else if (arg_end > 2) {
      not_int = 1;
    }
    if (not_int == 1) {
      fprintf(stderr, "Exit command not valid\n");
      dol_ques = 1;
      goto exit;
    }
    fprintf(stderr, "\nexit\n");

    //SIGINT all child processes;
    pid_t wpid;
    int status;
    
    //Code block derived from https://stackoverflow.com/questions/19461744/how-to-make-parent-wait-for-all-child-processes-to-finish
    while ((wpid = wait(&status)) > 0) {      
      if (kill(wpid, SIGINT) == -1) perror("Error in kill process: ");
    }
    if (arg_end == 1) exit(dol_ques);
    if (arg_end == 2) exit(atoi(wordArr[1]));
  }

  //Execute cd command
  if (strcmp(wordArr[0], "cd") == 0) {
    if (red_end == 1) {                 //No arguments provided. Change to HOME directory.
      if (chdir(getenv("HOME")) == -1) {
        perror("Could not get HOME env: ");
      }
    } else if (red_end == 2) {          //One argument provided. Try to change to that directory. 
      if (chdir(wordArr[1]) == -1) {
        errno = 0;
        fprintf(stderr, "%s is not a valid directory\n", wordArr[1]);
        dol_ques = 1;
      } 
    } else {                            //Too many arguments not a valid cd command.
      fprintf(stderr, "%s is not a valid directory\n", wordArr[1]);
      dol_ques = 2;
    }
    goto exit;
  }

  //Execute non-built in commands
  if (strchr(wordArr[0], '/') == NULL) {
    int childStatus;
    char *argArr[512];

    //Create array for arguments.
    for (int i = 0; i < arg_end; i++) {
      argArr[i] = wordArr[i];
      argArr[i+1] = NULL;
    }
    struct sigaction original = {0};
    pid_t spawnPid = fork();

    switch(spawnPid) {
    case -1:
      errno = 0;
      fprintf(stderr, "Error in forking process");
      dol_ques = 2;
      goto exit;
      break;

    case 0:
      // Child process
      
      //Implement signal reset to their original disposition
      sigaction(SIGINT, &original, &ignore_action);
      sigaction(SIGTSTP, &original, &ignore_action);

      //Implement redirection operators for reading/writing on stdin and stdout
      if (red_end != arg_end) {
        for (int i = arg_end; i < red_end-1; i++) {
          
          //Implement input redirection
          if (strcmp(wordArr[i], "<") == 0) {
            int sourceFD = open(wordArr[i+1], O_RDONLY);
            if (sourceFD == -1) {
              fprintf(stderr, "%s is not a valid file descriptor", wordArr[i+1]);
              exit(1);
            }
            //Redirect stdin to source file
            int result = dup2(sourceFD, 0);
            if (result == -1) {
              fprintf(stderr, "Error in redirecting stdin to open file");
              exit(2);
            }
          }

          //Implement output redirection
          if (strcmp(wordArr[i], ">") == 0) {
            int targetFD = open(wordArr[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
            if (targetFD == -1) {
              fprintf(stderr, "Error opening %s for writing",wordArr[i+1]);
              exit(1);
            }
            //Redirect stdout to target file
            int result = dup2(targetFD, 1);
            if (result == -1) {
              fprintf(stderr, "Error in redirecting stdout to open file");
              exit(2);
            }
          }
        }
      }
      //Run execvp on command with arguments.
      if (execvp(wordArr[0], argArr) == -1){
        fprintf(stderr, "Error executing command\n");
        exit(1);
      }
      break;

    default:
      //Parent process
      
      //If background process was not implemented, update according to specs.
      if (bgd == 0) {
        spawnPid = waitpid(spawnPid, &childStatus, 0);
        if (WIFEXITED(childStatus)) dol_ques = WEXITSTATUS(childStatus);
        if (WIFSIGNALED(childStatus)) dol_ques = 128 + WTERMSIG(childStatus);
        if (WIFSTOPPED(childStatus)) {
          kill(spawnPid, SIGCONT);
          fprintf(stderr, "Child process %d stopped. Continuing.\n", spawnPid);
          dol_exc = spawnPid;
        }
      } else {
        dol_exc = spawnPid;
      }
      goto exit;
      break;
    }
  }

exit:;
//  free_array(wordArr, array_size);
  free_array(wordArr, array_size);
  goto step1;
  }
exit(0);
}

// ~/ at beginning expansion function
char * home_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub) {
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle), sub_len = strlen(sub);
  ptrdiff_t off = str - *haystack;

  for (; (str = strstr(str, needle));) {
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }
    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
    break;
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }
exit:
  return str;
}

//String expansion function
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub) {
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle), sub_len  = strlen(sub);

  for (; (str = strstr(str, needle));) {
    ptrdiff_t off = str - *haystack;
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }
    
    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }

exit:
  return str;
}

void free_array(char **wordArr, int array_size) {
  for (int i = 0; i < array_size; i++) {
    free(wordArr[i]);
  }
}

