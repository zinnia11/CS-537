
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define ERR "An error has occurred\n"

char *path[32];

int process_command(char *tokens[], size_t token_n);
int parse_command(char *command, size_t length);
void free_path();

//////////////////////////////////////////////////////////////////////////

int ex_command(char *tokens[], size_t token_n, char* loc) {
	// check if the command path exists
	char **pathptr = path;
	if (*pathptr == NULL) {
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		return 1;
	}

	char *hold = malloc(strlen(*pathptr)+1);
	if (!hold) { // malloc fails
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		return 1;
	}

	short exist = 0;
	while (*pathptr != NULL) {
		// create the entire file name
		hold = realloc(hold, strlen(tokens[0]) + strlen(*pathptr) + 2);
		if (!hold) { // realloc fails
			write(STDERR_FILENO, ERR, strlen(ERR)); 
			return 1;
		}
		strcpy(hold, *pathptr);
		strcat(hold, "/");
		strcat(hold, tokens[0]);
		//printf("%s\n", hold);
		//printf("%d\n", strlen(hold));
		//printf("equivalence: %d\n", strcmp(hold, "/bin/echo"));
		//printf("access: %d\n", access(hold, X_OK));
		if (access(hold, X_OK) == 0) { // file exists
			exist = 1;
			break;
		}
		pathptr++;
	}
	if (exist == 0) {
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		free(hold);
		return 1;
	}

	// create child process and execute
	tokens[token_n] = NULL; // last element of the argument array must be NULL
	pid_t pid = fork();
	if (pid == 0) { // child
		if (loc != NULL) {
			int fd = open(loc, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		    dup2(fd, 1);   // make stdout go to file
		    dup2(fd, 2);   // make stderr go to file 
			close(fd);     // fd no longer needed
		}
	
	    execv(hold, tokens); // execv does not return on success
		free(hold); // free used mem
		exit(1); // exit on failure of execv() so child doesn't continue to run
	} else { // the parent 
		int status;
		waitpid(pid, &status, 0);
		free(hold); // free used mem
		return WEXITSTATUS(status);
	}

	return 0;
}


int ex_cd(char *tokens[], size_t token_n) {
	if (token_n <= 1 || token_n >= 3) { // too many or too few args
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		return 1;
	} 

	// change directory, if fails then return error
	if (chdir(tokens[1]) == -1) {
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		return 1;
	}

	return 0;
}

int ex_path(char *tokens[], size_t token_n) {
	free_path();
	char **tokenptr = tokens;
	for (int i=0; i<token_n-1; i++) {
		tokenptr++;
		// path[i] contains a pointer to the right size
		path[i] = malloc(strlen(*tokenptr)+1); 
		strcpy(path[i], *tokenptr);
	}

	return 0;
}

void free_path() {
	char **pathptr = path;
	while (*pathptr != NULL) {
		free(*pathptr);
		*pathptr = NULL;
		pathptr++;
	}
}

int ex_if(char *tokens[], size_t token_n) {
	if (strcmp(tokens[token_n-1], "fi") != 0) {
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		return 1;
	}

	char *first[token_n-2];
	char *second[token_n-2];
	char third = malloc(1);
	third[0] = '\0';
	int len1 = 0, len2 = 0, len3 = 0;
	// separate commands
	for (int i=1; i<token_n; i++) {
		// find the operator
		if (strcmp(tokens[i], "==")==0) {
			for (int j=i+1; j<token_n; j++) { // find the value
				if (strcmp(tokens[j], "then")==0) {
					// the then command
					for (int k=j+1; k<token_n-1; k++) {
						third = realloc(third, strlen(tokens[k]) + strlen(third) + 2);
						strcat(third, tokens[k]);
						strcat(third, " ");
						len3 = len3 + strlen(tokens[k]);
					}
					break;
				} else { 
					second[len2] = tokens[j];
					len2++;
				}
			}
			if (len2 != 1) { // want only a constant
				write(STDERR_FILENO, ERR, strlen(ERR)); 
				return 1;
			}

			// perform the if, then do the command
			len2 = strtol(second[0], NULL, 10);
			if (process_command(first, len1) == len2) {
				int ret = parse_command(third, len3);
				free(third);
				return ret;
			}
			break;
		} else if (strcmp(tokens[i], "!=")==0) {
			for (int j=i+1; j<token_n; j++) {
				if (strcmp(tokens[j], "then")==0) {
					// the then command
					for (int k=j+1; k<token_n-1; k++) {
						third = realloc(third, strlen(tokens[k]) + strlen(third) + 2);
						strcat(third, tokens[k]);
						strcat(third, " ");
						len3 = len3 + strlen(tokens[k]);
					}
					break;
				} else {
					second[len2] = tokens[j];
					len2++;
				}
			}
			if (len2 != 1) { // want only a constant
				write(STDERR_FILENO, ERR, strlen(ERR)); 
				return 1;
			}

			// perform the if, then do the command
			len2 = strtol(second[0], NULL, 10);
			if (process_command(first, len1) != len2) {
				int ret = parse_command(third, len3);
				free(third);
				return ret;
			}
			break;
		} else { // otherwise cat the argument to the first command
			first[len1] = tokens[i];
			len1++;
		}
	}

	if (len2 == 0) { // didn't get to the inner loops
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		free(third);
		return 1;
	}
	free(third);
	return 0;
}

int process_command(char *tokens[], size_t token_n) {
	if (token_n == 0) {
		return 1;
	}

	// perform whatever the command is
	switch(strlen(tokens[0])) {
		case 2:
			if (strcmp(tokens[0], "cd") == 0) { // built in command for cd
				return ex_cd(tokens, token_n);
				break;
			}
			if (strcmp(tokens[0], "if") == 0) { // built in command for if
				return ex_if(tokens, token_n);
				break;
			}
		case 4:
			if (strcmp(tokens[0], "exit") == 0) { // built in command for exit
				if (token_n != 1) {
					write(STDERR_FILENO, ERR, strlen(ERR)); 
					return 1;
				}
				free_path();
				
				exit(0);
				break;
			}
			if (strcmp(tokens[0], "path") == 0) { // built in command for path
				return ex_path(tokens, token_n);
				break;
			}
		default:
			return ex_command(tokens, token_n, NULL); // execute a command
	}

	return 0;
}

/*
 * First step in processing the command. Removes formatting from the input and
 * tokenizes the string before passing to the method to execute the command. 
 * @param: command is a char pointer to the user input
 * @param: length is the number of characters in the user input
 */
int parse_command(char *command, size_t length) {
	if (command == NULL) {
		return 1;
	}

	// remove \n char in command
	if (command[length-1] == '\n') {
		command[length-1] = '\0';
		length--;
	}

	// get first token
	char delim[] = " \t\r\n\v\f";
	char *tokens[length-1];
	char *save;
	tokens[0] = strtok_r(command, delim, &save);
	// put all tokens in an array
	size_t i = 0;
	while( tokens[i] != NULL ) {
		i++;
		// redirection
		if (strstr(tokens[i-1], ">") != NULL && strcmp(tokens[0], "if") != 0) {
			char *c;
			size_t index;
			c = strchr(tokens[i-1], '>');
			index = (c - tokens[i-1]);
			char *loc = NULL;

			// if the token starts with >, the loc is either the next token, or is
			// in the same token as the >
			if (index == 0) { 
				loc = strtok_r(NULL, delim, &save);
				if (loc == NULL) { // loc is not next token 
					if (strlen(tokens[i-1]) == 1) { // length 1 means on the > operator
						write(STDERR_FILENO, ERR, strlen(ERR)); 
						return 1;
					}
					loc = &tokens[i-1][1];
					if (strstr(loc, ">") != NULL) { // check for another > 
						write(STDERR_FILENO, ERR, strlen(ERR)); 
						return 1;
					}
				} else if (strcmp(loc, ">") == 0 || strstr(loc, ">") != NULL) {
					write(STDERR_FILENO, ERR, strlen(ERR)); 
					return 1;
				}
				i--; // remove the index of the >
			} else if (index == strlen(tokens[i-1])-1) { // > is the last index
				tokens[i-1][index] = '\0';
				loc = strtok_r(NULL, delim, &save);
				if (loc == NULL || strstr(loc, ">") != NULL) {
					write(STDERR_FILENO, ERR, strlen(ERR)); 
					return 1;
				}
			} else { // separate the end of the command and the filename from token
				tokens[i-1][index] = '\0';
				loc = &tokens[i-1][index+1];
				if (strstr(loc, ">") != NULL) {
					write(STDERR_FILENO, ERR, strlen(ERR)); 
					return 1;
				}
			}

			if (strtok_r(NULL, delim, &save) != NULL) { // more arguments after the filename
				write(STDERR_FILENO, ERR, strlen(ERR)); 
				return 1;
			}
			// execute the command with a location to put it
			return ex_command(tokens, i, loc);
		}

      	tokens[i] = strtok_r(NULL, delim, &save);
    }

    if (tokens[0] == NULL) {
    	return 1;
    }
    // perform whatever the command is
	return process_command(tokens, i);
}

/*
 * Print out a prompt and then reads in the command
 */
int main(int argc, char *argv[]) {
	// set the path to "/bin" to begin
	path[0] = malloc(sizeof("/bin"));
	strcpy(path[0], "/bin");

	char  *buffer = 0;
    size_t buflen = 0;
    size_t len = 0;
	if (argc == 1) {
		// no command line arguments: -> is in interactive mode
		while (1) {
			printf("wish> ");
			if ((len = getline(&buffer, &buflen, stdin)) == -1) {
				free_path();
				exit(0);
			}
			parse_command(buffer, len);
		}
	} else if (argc == 2) {
		// in batch mode
		FILE *fp = NULL;
		if (!(access(argv[1], F_OK) == -1)) { // file exists
			  fp = fopen(argv[1], "r");
			  if (!fp) {
			    write(STDERR_FILENO, ERR, strlen(ERR)); 
			    return 1;
			  }
		} else {
			write(STDERR_FILENO, ERR, strlen(ERR)); 
			return 1;
		}
		while (1) {
			if ((len = getline(&buffer, &buflen, fp)) == -1) {
				fclose(fp);
				free_path();
				exit(0);
			}
			parse_command(buffer, len);
		}
	} else {
		write(STDERR_FILENO, ERR, strlen(ERR)); 
		return 1;
	}

	free_path();
	return 0;
}

