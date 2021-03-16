#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
const char * sysname = "seashell";

#define HISTORYSIZE 5
#define BUFFERSIZE 4096

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	bool repeat;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};

struct hist {
	char commands[HISTORYSIZE][BUFFERSIZE];
	int length;
};

struct alias {
	char shortName[BUFFERSIZE];
	char longName[BUFFERSIZE];
	struct alias *next;
	struct alias *prev;
};

typedef struct hist history;

typedef struct alias shortdir;

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRepeating History: %s\n", command->repeat?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;
	if (len>0 && buf[len-1]=='!' && buf[len-2]=='!') // history
		command->repeat=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// 

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
			|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command, history *h)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	//static char history[5][4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


    //FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
  	while (1)
  	{
		c=getchar();
		//printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}

		if (c==27 && multicode_state==0) // (UP) handle multi-code keys
		{
			multicode_state=1;
			continue;
		}

		if (c==91 && multicode_state==1) // ( ) )  ?
		{
			multicode_state=2;
			continue;
		}

		if (c==65 && multicode_state==2) // unechoed A
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
  	}
  	if (index>0 && buf[index-1]=='\n') // trim newline from the end
  		index--;
  	buf[index++]=0; // null terminate string

  	//Push stack!
  	int ih = HISTORYSIZE-2;
  	for(;ih>=0;ih--){
  		strcpy(h->commands[ih+1], h->commands[ih]);
  	}

  	//FIXME: Consider unifying oldbuf and history?
  	strcpy(oldbuf, buf);
  	strcpy(h->commands[0], buf);

  	//Update length
  	//printf("LENGTH WAS: %d\n", h->length);
  	//FIXED OVERFLOW INTO h->length
	h->length = ( (h->length) < HISTORYSIZE ) ? (h->length+1) : HISTORYSIZE;
	//printf("LENTH IS NOW: %d\n", h->length);

  	parse_command(buf, command);

  	//print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  	return SUCCESS;
}
int process_command(struct command_t *command, history *h, shortdir *shortdirs);
int main()
{
	
	//INIT HISTORY
	history *h=malloc(sizeof(history));
	memset(h, 0, sizeof(history));

	//INIT ALIASES
	shortdir *shortdirs=malloc(sizeof(shortdir)); //shortdirs <- list of shortdirs
	memset(shortdirs, 0, sizeof(shortdir));

	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		//code = prompt(command);
		code = prompt(command,h);
		if (code==EXIT) break;

		//code = process_command(command);
		//code = process_command(command,h);
		code = process_command(command,h,shortdirs);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command, history *h, shortdir *shortdirs)
{
	char cwd[1024];

	//CHECK FOR REPEATS BEFORE FORKING
	if(command->repeat){

		// If HISTORY ARRAY IS EMPTY PRINT THE MESSAGE "No commands in history."

		if (h->length <= 1){
			printf("No commands in history.\n");
			//DELETE !! from history
			//printf("%s\n", h->commands[0]);
			h->length = 0;
			memset(h->commands[0], 0, sizeof(char) * BUFFERSIZE);
			//printf("%s\n", h->commands[0]);
			return SUCCESS;
		}
		
		// Else print the command from the top of the history.
		else{
			//char lastbuff[4096];
			//strcpy(oldbuff, lastbuff);
			//printf("%s\n",lastbuff);

			//h->commands[0] <- h->commands[1]
			strcpy(command->name, h->commands[1]);
			strcpy(h->commands[0], h->commands[1]);

			//printf("%d\n", h->length);
			//printf("There is a command in history but it's hidden from me :{\n");
		}

		// (NOT REQUIRED)
		// Replace all !!'s in the command with the string from the top of the history array.
		// Including the top of history!

	}

	int r;
	if (strcmp(command->name, "")==0) 
		return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}

	//PART II
	if (strcmp(command->name, "shortdir")==0)
	{
		if (command->arg_count > 0)
		{

			//printf("%s, %s\n", command->args[0], command->args[1]);

			if (strcmp(command->args[0], "set")==0 ){
				//printf("Not yet implemented\n" );
				//printf("%s %s\n", command->args[0], command->args[1]);
				if (!(command->args[1])){
					printf("error: name not specified for shortdir set.\n" );
					return UNKNOWN;
				}

				shortdir *s;
				s = shortdirs;
				for (; s->next->shortName != NULL ; s=s->next ) {
					//printf("SHIFTED\n" );
				}

				//ADD
				strcpy(s->shortName,command->args[1]);
			    strcpy(s->longName,getcwd(cwd,sizeof(cwd)));

			    //RESERVE NEXT ELEMENT
			    s->next=malloc(sizeof(shortdir));
			    memset(s->next, 0, sizeof(shortdir));
			    s->next->prev = s;

			    printf("%s is set as an alias for %s\n",s->shortName,s->longName);
			}
			else if (strcmp(command->args[0], "jump")==0 ){
				printf("Not yet implemented\n" );

				if (!(command->args[1])){
					printf("error: name not specified for shortdir set.\n" );
					return UNKNOWN;
				}
				shortdir *s;
				s = shortdirs;
				for (; s->next->shortName != NULL && s->next->shortName != command->args[1]; s=s->next ) {
					//printf("SHIFTED\n" );
				}
				//TODO: JUMP TO s->longName

			}
			else if (strcmp(command->args[0], "del")==0 ){
				printf("Not yet implemented\n" );

				if (!(command->args[1])){
					printf("error: name not specified for shortdir set.\n" );
					return UNKNOWN;
				}
				shortdir *s;
				s = shortdirs;
				for (; s->next->shortName != NULL && s->next->shortName != command->args[1]; s=s->next ) {
					//printf("SHIFTED\n" );
				}
				//TODO: REMOVE s from the list!
			}
			else if (strcmp(command->args[0], "clear")==0 ){
				printf("Not yet implemented\n" );
			}
			else if (strcmp(command->args[0], "list")==0 ){
				printf("Not yet implemented\n" );
			}

			return SUCCESS;
		}
	}

	if (strcmp(command->name, "history")==0)
	{
		//printf("Time to make history!\n");

	  	int ih = h->length - 1, L = h->length;
	  	for(;ih>=0;ih--){
	  		printf("%d %s\n", (L - ih), h->commands[ih]);
	  	}

		return SUCCESS;
	}


	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
	    //extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		command->args=(char **)realloc(
			command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		execvp(command->name, command->args); // exec+args+path
		exit(0);
		/// TODO: do your own exec with path resolving using execv()
		/// DONE

		char *environ = getenv("PATH");
		char *fileToCheck = environ;
		char split[] = ":";
		char *ptr = strtok(environ, split);

		while(ptr != NULL) {
		  	char *exeToCheck = strcat(fileToCheck, command->name);
		    if(access(exeToCheck, F_OK) == 0){
			   execv(exeToCheck, exeToCheck);
			   break;
		    } else {
			  printf("%s\n", "File does not exist");
		    }
			ptr = strtok(NULL, split);
		}

			
		
	} else {
		//Already Implemented
		if (!command->background){
			wait(0); // wait for child process to finish
		}
		return SUCCESS;
	}

	// TODO: your implementation here

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
