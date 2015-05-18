/*
	luksipc - Tool to convert block devices to LUKS in-place.
	Copyright (C) 2011-2015 Johannes Bauer

	This file is part of luksipc.

	luksipc is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; this program is ONLY licensed under
	version 3 of the License, later versions are explicitly excluded.

	luksipc is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with luksipc; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	Johannes Bauer <JohannesBauer@gmx.de>
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

#include "exec.h"
#include "logging.h"

#define MAXARGS			64

int argCount(const char **aArgs) {
	int cnt = 0;
	while (aArgs[cnt++]);
	return cnt - 1;
}

bool argAppend(const char **aArgs, const char *aNewArg, int *aArgCount, int aArraySize) {
	bool success = true;
	if ((*aArgCount) < 0) {
		*aArgCount = argCount(aArgs);
	}
	if ((*aArgCount + 2) > aArraySize) {
		/* Cannot copy next argument */
		success = false;
	} else {
		aArgs[*aArgCount] = aNewArg;
		(*aArgCount)++;
		aArgs[*aArgCount] = NULL;
	}
	return success;
}

bool argAppendParse(const char **aArgs, char *aNewArgs, int *aArgCount, int aArraySize) {
	bool success = true;
	char *savePtr = NULL;
	char *nextToken;
	if ((*aArgCount) < 0) {
		*aArgCount = argCount(aArgs);
	}
	while ((nextToken = strtok_r(aNewArgs, ",", &savePtr))) {
		if ((*aArgCount + 2) > aArraySize) {
			/* Cannot copy next argument */
			success = false;
			break;
		} else {
			aArgs[*aArgCount] = nextToken;
			(*aArgCount)++;
		}
		aNewArgs = NULL;
	}
	aArgs[*aArgCount] = NULL;
	return success;
}

void argDump(const char **aArgs) {
	int i = 0;
	while (aArgs[i]) {
		printf("   %2d: '%s'\n", i, aArgs[i]);
		i++;
	}
}

static char **argCopy(const char **aArgs) {
	char **result = NULL;
	int i;
	result = malloc(sizeof(char*) * MAXARGS);
	if (!result) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	result[MAXARGS - 1] = NULL;
	for (i = 0; i < MAXARGS - 1; i++) {
		if (aArgs[i] == NULL) {
			result[i] = NULL;
			break;
		}
		result[i] = strdup(aArgs[i]);
		if (!result[i]) {
			perror("strdup");
			exit(EXIT_FAILURE);
		}
	}
	return result;
}

static void freeArgCopy(char** aArgCopy) {
	int i;
	for (i = 0; i < MAXARGS - 1; i++) {
		if (aArgCopy[i] == NULL) {
			break;
		}
		free(aArgCopy[i]);
	}
	free((void*)aArgCopy);
}

int execGetReturnCode(const char **aArguments) {
	char **argcopy = argCopy(aArguments);
	pid_t pid;
	int status;

	pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		logmsg(LLVL_DEBUG, "Subprocess [PID %d]: Will execute %s\n", pid, aArguments[0]);
	}
	if (pid == 0) {
		/* Child */
		if (getLogLevel() < LLVL_DEBUG) {
			/* Shut up the child */
			close(1);
			close(2);
		}
		execvp(aArguments[0], argcopy);
		perror("execvp");
		exit(EXIT_FAILURE);
	}

	if (waitpid(pid, &status, 0) == (pid_t)-1) {
		perror("waitpid");
		exit(EXIT_FAILURE);
	}

	freeArgCopy(argcopy);
	int returnCode = WEXITSTATUS(status);
	logmsg(LLVL_DEBUG, "Subprocess [PID %d]: %s returned %d\n", pid, aArguments[0], returnCode);
	return returnCode;
}

