#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

pid_t run_command_in_subprocess(char *file, char **argv, int files_index, int pipefd[2]);
bool printout_terminated_subprocess(int status, int pipe_read_fd, char *file);

int main(int argc, char *argv[]) {
	
	const char USAGE[] = "runpar: Usage: runpar NUMCORES COMMAND... _files_ FILE...\n";
	bool has_errored = false;
	
	int num_cores; // NUMCORES
	
	int files_index = -1; // index of _files_ divider
	int files_start_index; // starting index of files
	
	int number_of_files;

	if (argc < 5) { //simple test to determine quick exit
		fprintf(stderr, USAGE);
		exit(EXIT_FAILURE);
	}
	
	if ((num_cores = atoi(argv[1])) == 0) {
		fprintf(stderr, USAGE);
		exit(EXIT_FAILURE);
	}
	
	
	for (int i = 2; i < argc; i++) {
		if (strcmp(argv[i], "_files_") == 0) {
			files_index = i;
			files_start_index = i+1;
			break;
		}
	}
	if (files_index == -1 || strcmp(argv[2], "_files_") == 0) {
		fprintf(stderr, USAGE);
		exit(EXIT_FAILURE);
	}
		
	number_of_files = argc - files_start_index;
	if (number_of_files <= 0) {
		fprintf(stderr, USAGE);
		exit(EXIT_FAILURE);
	}
	
	char *file_args[number_of_files]; 
	for (int i = 0; i < number_of_files; i++) {
		file_args[i] = argv[files_start_index+i];
	}
	
	// set file start for use in execvp.
	argv[files_start_index] = NULL; 

	pid_t child_pid;
	pid_t ended_pid;
	int pipe_index;
	
	int status;

	//these three will be aligned 
	pid_t active_pipes[num_cores];
	char *file_to_pipes[num_cores];
	int pipes[num_cores][2];
	for (int i = 0; i < num_cores; i++) {
		pipe2(pipes[i], O_NONBLOCK);
	}
	
	
	for (int i = 0; i < number_of_files; i++) {
		if (i < num_cores) {
			pipe_index = i;
		} else {
			ended_pid = wait(&status);
			for (int j = 0; j < num_cores; j++) {
				if (ended_pid == active_pipes[j]) {
					pipe_index = j;
					break;
				}
			}
			if (printout_terminated_subprocess(status, pipes[pipe_index][0], file_to_pipes[pipe_index]) == true)
				has_errored = true;
		}
		child_pid = run_command_in_subprocess(file_args[i], argv, files_index, pipes[pipe_index]);
		active_pipes[pipe_index] = child_pid;
		
		file_to_pipes[pipe_index] = file_args[i];
	}
	
	//clean up remaining children.
	while ((ended_pid = wait(&status)) != -1) { //wait shouldnt fail any other way besides ECHILD
		for (int j = 0; j < num_cores; j++) {
			if (ended_pid == active_pipes[j]) {
					pipe_index = j;
					break;
				}
		}
		if (printout_terminated_subprocess(status, pipes[pipe_index][0], file_to_pipes[pipe_index]) == true)
				has_errored = true;
	}
	
	
	for (int i = 0; i < num_cores; i++) {
		close(pipes[i][0]);
		close(pipes[i][1]);
	}
	
	if (has_errored)
		exit(EXIT_FAILURE);
	else
		exit(EXIT_SUCCESS);
}

pid_t run_command_in_subprocess(char *file, char **argv, int files_index, int pipefd[2]) {
	pid_t pid;
	
	argv[files_index] = file;
	
	pid = fork();
	
	if (pid == -1) { //error
		fprintf(stderr, "runpar: failed to create subprocess for %s.\n", file);
		exit(EXIT_FAILURE);
	} else if (pid == 0) { //child
		dup2(pipefd[1], 1);
		dup2(pipefd[1], 2);
		close(pipefd[0]);
		close(pipefd[1]);
			
		execvp(argv[2], &argv[2]);
		exit(EXIT_FAILURE);
	} else { //parent
		return pid;
	}
}

bool printout_terminated_subprocess(int status, int pipe_read_fd, char *file) {
	int nread;
	char buff[4096];
	bool has_errored = false;
	
	printf("--------------------\n");
	printf("Output from: %s\n", file);
	printf("--------------------\n");
	while ((nread = read(pipe_read_fd, buff, 4096)) > 0) {
		write(1, buff, nread);
	}
	
	if (WEXITSTATUS(status) != 0)
		has_errored = true;
	
	return has_errored;
}
