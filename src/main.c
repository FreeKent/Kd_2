
#define _GNU_SOURCE
#include <kd_2.h>
#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <time.h>

static const struct option opts[] = {
  {"logfile", required_argument, NULL, 1},
  {"execute", required_argument, NULL, 2},
  {"multiplex", required_argument, NULL, 3}
};

char *logFile, *command="bc";
int mode = 1;
int receivedChildSignal = 0;
siginfo_t sigchldInfo,sigIOInfo;
char str[10000];
int receivedIOSignal = 0,receivedOutSignal = 0,receivedErrSignal = 0;
int needIn=0, needOut=0, needErr=0;
int fdin[2],fdout[2],fderr[2];
int fdLog;

void childHandler(int signo, siginfo_t *siginfo, void *context){
  switch (signo) {
    case SIGCHLD:{
      receivedChildSignal = 1;
      sigchldInfo = *siginfo;
      break;
    }
    case SIGIO:{
      receivedIOSignal = 1;
      sigIOInfo = *siginfo;
      if (sigIOInfo.si_fd == 0) {//si_fd
        needIn = 1;
      } else if (sigIOInfo.si_fd == fdout[0]) {
        needOut = 1;
      } else if (sigIOInfo.si_fd == fderr[0]) {
        needErr = 1;
      }
      break;
    }
    default:
      break;
  }
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
  fdLog = (logFile == NULL) ? 2 : open(logFile, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
  if (fdLog == -1) {
    perror("LogFile error:\n");
  }
  printf("------======Launching======------\n");
  return 0;
}

void getTime(char *timeStr) {
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  strftime(timeStr, 18, "%D %H:%M:%S", &tm);
}

void logStr(char *str, char *directionStr){
  char timeStr[18];
  getTime(timeStr);
  write(fdLog, timeStr , strlen(timeStr));
  write(fdLog, directionStr, strlen(directionStr));
  write(fdLog, str, strlen(str));
}

int main (int argc, char *argv[]) {
  
  if (getArgs(argc, argv) == 1){
    return 0;
  }
  
  
  if (pipe(fdin)) {
    perror("Can't create pipe\n");
    return 0;
  }
  if (pipe(fdout)) {
    perror("Can't create pipe\n");
    return 0;
  }
  if (pipe(fderr)) {
    perror("Can't create pipe\n");
    return 0;
  }
  int childPid = fork();
  if (childPid == -1) {
    perror("Can't fork:\n");
    return 0;
  } else if (childPid == 0) {
    
    char *cmdName = strtok(command, " ");
    char *cmdArgs = strtok(NULL, "");
    char *cmdArgv[] = {cmdName, cmdArgs, NULL};
    if(dup2(fdout[1],1)==-1){
      perror("Can't redirect stream:\n");
      return 0;
    }
    if(dup2(fderr[1],2)==-1){
      perror("Can't redirect stream:\n");
      return 0;
    }
    if(dup2(fdin[0],0)==-1){
      perror("Can't redirect stream:\n");
      return 0;
    }
    close(fdout[0]);
    close(fderr[0]);
    close(fdin[1]);
    close(fdout[1]);
    close(fderr[1]);
    close(fdin[0]);
    if(execvp(cmdName,cmdArgv) == -1){
      perror("Can't execute:\n");
      return 0;
    }else {
      printf("ALL GOOD");
    }
  } else {
    struct sigaction sa;
    sa.sa_sigaction = &childHandler;
    sa.sa_flags = SA_SIGINFO;
    sigset_t mask;
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGIO);
    sa.sa_mask = mask;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
      perror("Can't change action for SIGCHLD");
    }
    if (sigaction(SIGIO, &sa, NULL) == -1) {
      perror("Can't change action for SIGIO");
    }
    
    close(fdout[1]);
    close(fderr[1]);
    close(fdin[0]);
    if (mode == 0) {
      fcntl(0, F_SETFL, O_ASYNC | O_NONBLOCK);
      fcntl(fdout[0], F_SETFL, O_ASYNC | O_NONBLOCK);
      fcntl(fderr[0], F_SETFL, O_ASYNC | O_NONBLOCK);
      
      if(fcntl(0, F_SETSIG, SIGIO) == -1){
        perror("Can't SETSIG:\n");
      }
      if(fcntl(fdout[0], F_SETSIG, SIGIO) == -1){
        perror("Can't SETSIG:\n");
      }
      if(fcntl(fderr[0], F_SETSIG, SIGIO) == -1){
        perror("Can't SETSIG:\n");
      }
      fcntl(0, F_SETOWN, getpid());
      fcntl(fdout[0], F_SETOWN, getpid());
      fcntl(fderr[0], F_SETOWN, getpid());
    }
    
    while (!receivedChildSignal) {
      fd_set fds;
      struct timeval tv;
      int fdCount = 0;
      if (mode == 0) {
        sleep(1);
      } else {
        FD_ZERO(&fds);
        FD_SET(fdout[0], &fds);
        FD_SET(fderr[0], &fds);
        FD_SET(0, &fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        fdCount = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
      }
      if (fdCount == -1) {
        perror("Select error:\n");
      } else if (fdCount == 0 && !receivedIOSignal){
        char timeStr[18];
        getTime(timeStr);
        write(fdLog, timeStr , strlen(timeStr));
        write(fdLog, " NOIO\n", 7);
      } else {
        char buffer[1];
        int strLength;
        if (mode == 0) {
          
        } else {
          needIn = FD_ISSET(0, &fds);
          needOut = FD_ISSET(fdout[0], &fds);
          needErr = FD_ISSET(fderr[0], &fds);
        }
        if (needIn){
          needIn = 0;
          struct pollfd temp;
          temp.fd = 0;
          temp.events = POLLIN;
          if (poll(&temp, 1, 0)==1) {
            strLength=0;
            int readRes = read(0, &buffer, sizeof(char));
            if (readRes == -1) {
              perror("Can't read:\n");
              return 0;
            }
            
            while(readRes>0) {
              str[strLength++]=buffer[0];
              if (poll(&temp, 1, 0)==1) {
                readRes = read(0, &buffer, 1);
                if (readRes == -1) {
                  perror("Can't read:\n");
                  return 0;
                }
              } else {
                if (strLength>0) {
                  str[strLength]='\0';
                  //printf("In: %s\n",str);
                  //strLength = 0;
                }
                break;
              }
            }
            
            
            printf("%d / >0 / %s",childPid,str);
            logStr(str, " >0 / ");
            if (strncmp(str,"exit",4) == 0 && strLength == 5) {
              printf("kill child\n");
              kill(SIGKILL,childPid);
              break;
            }
            
            int writeRes = write(fdin[1],str,strLength);
            if(writeRes==-1){
              perror("Can't write to child:\n");
              return 0;
            } else if (writeRes == 0){
              fprintf (stderr, "Didn't write anything\n");
            } else {
              //printf ("writed %d\n",writeRes);
            }
          }
        }
        if (needOut) {
          needOut = 0;
          strLength=0;
          int readRes = read(fdout[0], &buffer, sizeof(char));
          if (readRes == -1) {
            perror("Can't read:\n");
            return 0;
          }
          struct pollfd temp;
          temp.fd = fdout[0];
          temp.events = POLLIN;
          while(readRes>0) {
            
            if (buffer[0]!='\n') {
              str[strLength++]=buffer[0];
            } else {
              str[strLength++]='\n';
              str[strLength]='\0';
              printf("%d / <1 / %s",childPid,str);
              
              logStr(str, " <1 / ");
              
              strLength = 0;
            }
            
            if (poll(&temp, 1, 0)==1) {
              readRes = read(fdout[0], &buffer, 1);
              if (readRes == -1) {
                perror("Can't read:\n");
                return 0;
              }
            } else {
              if (strLength>0) {
                str[strLength++]='\n';
                str[strLength]='\0';
                printf("%d / <1 / %s",childPid,str);
                logStr(str, " <1 / ");
                strLength = 0;
              }
              break;
            }
            
          }
          receivedIOSignal = 0;
        }
        if (needErr) {
          needErr = 0;
          strLength=0;
          int readRes = read(fderr[0], &buffer, sizeof(char));
          if (readRes == -1) {
            perror("Can't read:\n");
            return 0;
          }
          struct pollfd temp;
          temp.fd = fderr[0];
          temp.events = POLLIN;
          while(readRes>0) {
            if (buffer[0]!='\n') {
              str[strLength++]=buffer[0];
            } else {
              str[strLength++]='\n';
              str[strLength]='\0';
              printf("%d / <2 / %s",childPid,str);
              logStr(str, " <2 / ");
              strLength = 0;
            }
            if (poll(&temp, 1, 0)==1) {
              readRes = read(fderr[0], &buffer, 1);
              if (readRes == -1) {
                perror("Can't read:\n");
                return 0;
              }
            } else {
              if (strLength>0) {
                str[strLength++]='\n';
                str[strLength]='\0';
                printf("%d / <2 / %s",childPid,str);
                logStr(str, " <2 / ");
                strLength = 0;
              }
              break;
            }
          }
        }
        
      }
    }
    printf("%d TERMINATED WITH CODE %d\n", childPid, sigchldInfo.si_code);
  }
  
  return 0;
}
