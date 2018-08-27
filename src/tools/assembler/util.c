#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

void pathNormalise(char *path) {
	// Add trailing slash to make . and .. logic simpler
	// TODO: Fix this hack (we may access one beyond what path allows)
	size_t preLen=strlen(path);
	path[preLen]='/';
	path[preLen+1]='\0';

	bool change;
	do {
		char *c;
		change=false;

		// Replace '/x/../' with '/'
		c=path;
		while((c=strstr(c, "/../"))!=NULL) {
			// Look for last slash before this
			char *d;
			for(d=c-1; d>=path; --d) {
				if (*d=='/')
					break;
			}

			change=true;
			memmove(d, c+3, strlen(c+3)+1);

			++c;
		}

		// Replace '/./' with '/'
		c=strstr(path, "/./");
		if (c!=NULL) {
			change=true;
			memmove(c, c+2, strlen(c+2)+1);
		}

		// Replace '//' with '/'
		c=strstr(path, "//");
		if (c!=NULL) {
			change=true;
			memmove(c, c+1, strlen(c+1)+1);
		}
	} while(change);

	// Trim trailing slash (except for root)
	if (strcmp(path, "/")!=0 && path[strlen(path)-1]=='/')
		path[strlen(path)-1]='\0';
}

bool pathExists(const char *path) {
	assert(path!=NULL);

	return (access(path, F_OK)==0);
}
