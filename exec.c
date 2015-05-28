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
#include <errno.h>

#include "exec.h"
#include "logging.h"
#include "globals.h"

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
	result = malloc(sizeof(char*) * EXEC_MAX_ARGCNT);
	if (!result) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	result[EXEC_MAX_ARGCNT - 1] = NULL;
	for (i = 0; i < EXEC_MAX_ARGCNT - 1; i++) {
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
	for (i = 0; i < EXEC_MAX_ARGCNT - 1; i++) {
		if (aArgCopy[i] == NULL) {
			break;
		}
		free(aArgCopy[i]);
	}
	free((void*)aArgCopy);
}

static void convertCommandLine(char *aBuffer, int aBufSize, const char **aArguments) {
	if ((!aBuffer) || (aBufSize < 4)) {
		return;
	}
	aBuffer[0] = 0;

	int remaining = aBufSize - 4;
	int position = 0;
	int i = 0;
	bool truncated = false;
	while (aArguments[i]) {
		int newChars = snprintf(aBuffer + position, remaining, "%s ", aArguments[i]);
		if (newChars >= remaining) {
			truncated = true;
			break;
		}
		position += newChars;
		remaining -= newChars;
		i++;
	}
	if (truncated) {
		strcpy(aBuffer + aBufSize - 5, "...");
	} else {
		aBuffer[position - 1] = 0;
	}
}

struct execResult_t execGetReturnCode(const char **aArguments) {
	struct execResult_t execResult;
	char **argcopy = argCopy(aArguments);
	pid_t pid;
	int status;

	memset(&execResult, 0, sizeof(execResult));
	execResult.success = true;

	pid = fork();
	if (pid == -1) {
		perror("fork");
		execResult.success = false;
		return execResult;
	}
	if (pid > 0) {
		char commandLineBuffer[256];
		convertCommandLine(commandLineBuffer, sizeof(commandLineBuffer), aArguments);
		logmsg(LLVL_DEBUG, "Subprocess [PID %d]: Will execute '%s'\n", pid, commandLineBuffer);
	}
	if (pid == 0) {
		/* Child */
		if (getLogLevel() < LLVL_DEBUG) {
			/* Shut up the child if user did not request debug output */
			close(1);
			close(2);
		}
		execvp(aArguments[0], argcopy);
		perror("execvp");
		logmsg(LLVL_ERROR, "Execution of %s in forked child process failed at execvp: %s\n", aArguments[0], strerror(errno));

		/* Exec failed, terminate chExec failed, terminate child process
		 * (parent will catch this as the return code) */
		exit(EXIT_FAILURE);
	}

	if (waitpid(pid, &status, 0) == (pid_t)-1) {
		perror("waitpid");
		execResult.success = false;
		return execResult;
	}

	freeArgCopy(argcopy);
	execResult.returnCode = WEXITSTATUS(status);
	logmsg(LLVL_DEBUG, "Subprocess [PID %d]: %s returned %d\n", pid, aArguments[0], execResult.returnCode);
	return execResult;
}


