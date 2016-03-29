
#include <kd_2.h>
#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

static const struct option opts[] = {
  {"logfile", required_argument, NULL, 1},
  {"execute", required_argument, NULL, 2},
  {"multiplex", required_argument, NULL, 3}
};

char *logFile, *command;
int mode = 1;
int receivedChildSignal = 0;
siginfo_t sigchldInfo;
char str[10000];

void childHandler(int signo, siginfo_t *siginfo, void *context){
  receivedChildSignal = 1;
  sigchldInfo = *siginfo;
}

int getArgs(int argc, char *argv[]){
  int curOpt = 0;
  int index = 0;
  printf("-------=======Kd_2=======-------\n");
  printf("Pid - %i\n", getpid());
  printf("Arguments:\n");
  
  curOpt = getopt_long( argc, argv, "", opts, &index );
  while (curOpt != -1) {
    if (opts[index].val == 3) {
      mode = atoi(optarg);
      printf("Mode - %d\n", mode );
    } else if (opts[index].val == 1) {
      logFile = optarg;
      printf("Log file - %s\n", logFile );
    } else if (opts[index].val == 2) {
      command = optarg;
      printf("Command - %s\n", command );
    }
    curOpt = getopt_long( argc, argv, "", opts, &index);
  }
  
  if (command == NULL) {
    fprintf(stderr, "You must use --execute=\"command and args\"\n");
    return 1;
  }
  printf("------======Launching======------\n");
  return 0;
}

void main (int argc, char *argv[]) {
  
  if (getArgs(argc, argv) == 1){
    return;
  }
  
  int fdin[2],fdout[2],fderr[2];
  if (pipe(fdin)) {
    perror("Can't create pipe\n");
    return;
  }
  if (pipe(fdout)) {
    perror("Can't create pipe\n");
    return;
  }
  if (pipe(fderr)) {
    perror("Can't create pipe\n");
    return;
  }
  int childPid = fork();
  if (childPid == -1) {
    perror("Can't fork:\n");
    return;
  } else if (childPid == 0) {
    close(fdout[0]);
    close(fderr[0]);
    close(fdin[1]);
    char *cmdName = strtok(command, " ");
    char *cmdArgs = strtok(NULL, "");
    char *cmdArgv[] = {cmdName, cmdArgs, NULL};
    if(dup2(fdout[1],1)==-1){
      perror("Can't redirect stream:\n");
      return;
    }
    if(dup2(fderr[1],2)==-1){
      perror("Can't redirect stream:\n");
      return;
    }
    if(dup2(fdin[0],0)==-1){
      perror("Can't redirect stream:\n");
      return;
    }
    if(execvp(cmdName,cmdArgv) == -1){
      perror("Can't execute:\n");
      return;
    }else {
      close(fdout[1]);
      close(fderr[1]);
      close(fdin[0]);
      printf("ALL GOOD");
    }
  } else {
    struct sigaction sa;
    sa.sa_sigaction = &childHandler;
    sa.sa_flags = SA_SIGINFO;
    sigset_t mask;
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sa.sa_mask = mask;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
      perror("Can't change action for SIGCHLD");
    }
    
    close(fdout[1]);
    close(fderr[1]);
    close(fdin[0]);
    while (!receivedChildSignal) {
      fd_set fds;
      struct timeval tv;
      FD_ZERO(&fds);
      FD_SET(fdout[0], &fds);
      FD_SET(fderr[0], &fds);
      FD_SET(0, &fds);
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      int fdCount = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
      if (fdCount == -1) {
        perror("Select error:\n");
      } else if (fdCount == 0){
        printf("NOIO\n");
      } else {
        //printf ("IO!\n");
        char buffer[1];
        
        int strLength;
        if (FD_ISSET(fdout[0], &fds)) {
          strLength=0;
          printf("output\n");
          int readRes = read(fdout[0], &buffer, sizeof(char));
          if (readRes == -1) {
            perror("Can't read:\n");
            return;
          }
          while(readRes>0) {
            
            if (buffer[0]!='\n') {
              str[strLength++]=buffer[0];
              printf("strl %d\n",strLength);
            } else {
              str[strLength]='\0';
              printf("Out: %s\n",str);
              strLength = 0;
            }
            readRes = read(fdout[0], &buffer, sizeof(char));
            if (readRes == -1) {
              perror("Can't read:\n");
              return;
            }
          }
        }
        if (FD_ISSET(fderr[0], &fds)) {
          strLength=0;
          while(read(fderr[0], &buffer, sizeof(char)) > 0) {
            if (buffer[0]!='\n') {
              str[strLength++]=buffer[0];
            } else {
              str[strLength]='\0';
              fprintf(stderr,"Error: %s\n",str);
              strLength = 0;
            }
          }
        }
        if (FD_ISSET(0, &fds)) {
          strLength=0;
          //printf("input\n");
          while(read(0, &buffer, sizeof(char)) > 0/* && buffer[0] != '\n'*/) {
            str[strLength++]=buffer[0];
            if (buffer[0] == '\n') {
              break;
            }
          }
          //str[strLength++]='\n';
          //str[strLength++]='\0';
          printf("In: %s\n",str);
          if (strncmp(str,"exit",4) == 0) {
            printf("kill child\n");
            kill(SIGHUP,childPid);
            break;
          }
          printf ("not exit\n");
          
          int writeRes = write(fdin[1],str,strLength);
          if(writeRes==-1){
            perror("Can't write to child:\n");
            return;
          } else if (writeRes == 0){
            fprintf (stderr, "Didn't write anything\n");
          } else {
            printf ("writed %d\n",writeRes);
          }
          
          
        }
      }
    }
    printf("%d TERMINATED WITH CODE %d\n", sigchldInfo.si_pid, sigchldInfo.si_code);
  }
  
  return;
}
