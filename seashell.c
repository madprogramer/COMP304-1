#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
const char * sysname = "seashell";
const char * aliasfile = "/aliases.txt";
const char * alarmfile = "/alarm.txt";
//const char * aliasfile = "aliases.txt";
//const char * alarmfile = "alarm.txt";


#define HISTORYSIZE 20
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

// HIST TESTING

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
//PROTOTYPES
int process_command(struct command_t *command, history *h, shortdir *shortdirs);
int save_aliases(shortdir *shortdirs);
void load_aliases(shortdir *shortdirs);

int main()
{
	//INIT HISTORY
	history *h=malloc(sizeof(history));
	memset(h, 0, sizeof(history));

	//INIT ALIASES
	shortdir *shortdirs=malloc(sizeof(shortdir)); //shortdirs <- list of shortdirs
	memset(shortdirs, 0, sizeof(shortdir));
	load_aliases(shortdirs);
	//atexit(save_aliases(shortdirs));
	//buggy because of forks exitting!

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

		//SAVE SESSION
		save_aliases(shortdirs);
	}
	//SAVE ALIASES
	save_aliases(shortdirs);
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

	//INSTANT BUILT-INS

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

		//OUR BUILT-IN COMMANDS GO HERE

		//PART II
		if (strcmp(command->args[0], "shortdir")==0)
		{
			//printf("SHORTDIRS POINTER: %p\n", (void*)&shortdirs);
			if (command->arg_count > 0)
			{

				//printf("%s, %s\n", command->args[0], command->args[1]);

				if (strcmp(command->args[1], "set")==0 ){
					//printf("Not yet implemented\n" );
					//printf("%s %s\n", command->args[0], command->args[1]);
					if (!(command->args[1])){
						printf("error: name not specified for shortdir set.\n" );
						return UNKNOWN;
					}

					shortdir *s;
					s = shortdirs;

					for (; s->next != NULL && strcmp(s->shortName, command->args[2])!=0; s=s->next );

					//printf("%s %s\n", s->shortName, command->args[2]);

					//OVERWRITE DEFINITION
					if( strcmp(s->shortName, command->args[2])==0 ){
						//printf("Overwriting: %s with %s \n", s->longName, getcwd(cwd,sizeof(cwd)));
						strcpy(s->shortName,command->args[2]);
					    strcpy(s->longName,getcwd(cwd,sizeof(cwd)));
					}

					else{
						//printf("Writing: %s\n", getcwd(cwd,sizeof(cwd)));

						//ADD NEW DEFINITION
						strcpy(s->shortName,command->args[2]);
					    strcpy(s->longName,getcwd(cwd,sizeof(cwd)));

					    //printf("WRITTEN SUCCESSFULLY?\n");

					    //RESERVE NEXT ELEMENT
					    s->next=malloc(sizeof(shortdir));
					    memset(s->next, 0, sizeof(shortdir));
					    s->next->prev = s;
					}

				    printf("%s is set as an alias for %s\n",s->shortName,s->longName);
				}
				else if (strcmp(command->args[1], "jump")==0 ){
					//printf("Not yet implemented\n" );

					if (!(command->args[2])){
						printf("E: name not specified for shortdir jump.\n" );
						return UNKNOWN;
					}
					shortdir *s;
					s = shortdirs;
					for (; s->next != NULL && strcmp(s->shortName, command->args[2])!=0; s=s->next ) {
						//printf("SHIFTED\n" );
						//printf("%s : %s\n", s->shortName, s->longName);
						//printf("%s : %s\n", s->shortName, command->args[1]);
					}

					if( strcmp(s->shortName, command->args[2])!=0 ){
						printf("E: alias %s not found.\n", command->args[2] );
						return UNKNOWN;
					}

					//printf("%s : %s\n", s->shortName, s->longName);
					r=chdir(s->longName);
					if (r==-1)
						printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
					return SUCCESS;
				}
				else if (strcmp(command->args[1], "del")==0 ){
					//printf("Not yet implemented\n" );

					if (!(command->args[2])){
						printf("E: name not specified for shortdir del.\n" );
						return UNKNOWN;
					}

					shortdir *s;
					s = shortdirs;
					int isHead = 1;

					for (; s->next != NULL && strcmp(s->shortName, command->args[2])!=0; s=s->next ) {
						//printf("SHIFTED\n" );
						isHead = 0;
					}

					if(strcmp(s->shortName, command->args[2])!=0){
						printf("E: shortdir alias %s not found.\n", command->args[2]);
						return UNKNOWN;
					}

					if (!(isHead)){
						//printf("REMOVED NOT HEAD");
						//REMOVE S
						shortdir *head = s->prev, *tail = s->next;
						head->next = tail; tail->prev = head;
						free(s);
					}
					else{
						//printf("REMOVED HEAD");
						//SHIFT SHORTDIRS POINTER!
						shortdir *tail = s->next;

						//COPY VALUES
						strcpy(shortdirs->shortName,tail->shortName);
						strcpy(shortdirs->longName,tail->longName);

						shortdirs->next = tail->next;

						if( tail->next == 0){
							//NOT NULL BECAUSE WE MEMSET 0
							//printf("ONLY ONE ENTRY\n");
						}
						else{
							//printf("MORE THAN ONE ENTRY\n");
							tail->next->prev = shortdirs;
						}
						
						free(tail);
					}
				}
				else if (strcmp(command->args[1], "clear")==0 ){
					shortdir *s = shortdirs;
					for (; s->next != NULL; s=s->next ) {
					}

					//REVERSE
					for (; s != shortdirs; ) {
						free(s);
						s=s->prev;
					}

					//AT SHORTDIRS
					memset(shortdirs,0,sizeof(shortdir));
				}
				else if (strcmp(command->args[1], "list")==0 ){
					shortdir *s;
					s = shortdirs;
					for (; s->next->shortName != NULL ; s=s->next ) {
						printf("%s is an alias for %s\n", s->shortName, s->longName );
					}
				}

				return SUCCESS;
			}
		}

		//PART I (No longer mandatory)
		else if (strcmp(command->args[0], "history")==0)
		{
			//printf("Time to make history!\n");

		  	int ih = h->length - 1, L = h->length;
		  	for(;ih>=0;ih--){
		  		printf("%d %s\n", (L - ih), h->commands[ih]);
		  	}

			return SUCCESS;
		}

		//PART III: Word finder for highlighting
		else if (strcmp(command->args[0], "highlight")==0)
		{
			//printf("%d\n", command->arg_count);
			if (command->arg_count == 5) {

				char word[2048];
				strcpy(word,command->args[1]);

				char color[2];
				strcpy(color,command->args[2]);

				char filename[2048];
				strcpy(filename,command->args[3]);

	    		char * line = NULL;
	    		size_t len = 0;
	    		ssize_t read;

	    		FILE *f = fopen(filename, "r");
	    		if (f == NULL)
	        		exit(EXIT_FAILURE);

	        	int linecount=0;
				//getting each line
	   			while ((read = getline(&line, &len, f)) != -1) {

	   				//printf("Line %d\n", ++linecount);
	   				//strstrip(line);

					//Checks that the line should be printed or not
					int stringsOfColor = 0;
	        		//tokenizing string
					char *token = strtok(line, " \t\n");
					//we are copying whole line to this one token by token
					char lineAbouttaBePrinted[4096]="";
					//memset(lineAbouttaBePrinted,0,4096*sizeof(char));
					//going through tokens until the end of line
					while(token != NULL) {
						//checking whether this token is what we are looking for
						if(strcasecmp(token, word) == 0) {
							//Turn the string into red
							if(strcmp(color, "r") == 0) {
								char red[512] = "\e[31m\e[5m\e[1m";
								strcat(red, token);
								strcat(red, "\033[1m\033[0m");
								token = red;
								stringsOfColor = 1;
							}
							//Turn the string into green
							if(strcmp(color, "g") == 0) {
								char green[512] = "\e[32m\e[5m\e[1m";
								strcat(green, token);
								strcat(green, "\033[1m\033[0m");
								token = green;
								stringsOfColor = 1;
							}
							//Turn the string into blue
							if(strcmp(color, "b") == 0) {
								char blue[512] = "\e[34m\e[5m\e[1m";
								strcat(blue, token);
								strcat(blue, "\033[1m\033[0m");
								token = blue;
								stringsOfColor = 1;
							}
						}
						//Adding the token to the reconstructed line
						strcat(lineAbouttaBePrinted, token);
						strcat(lineAbouttaBePrinted, " ");
						//Tokenizing for the next loop
						token = strtok(NULL, " \t\n");
					}
					//If stringsOfColor exists we are printling the whole line
					if(stringsOfColor == 1) {
						printf("%s\n",lineAbouttaBePrinted);
					}
	    		}

	    		fclose(f);

	    		if (line)
	        		free(line);
			}
			return SUCCESS;
		}

		//PART IV: alarm
		else if (strcmp(command->args[0], "goodMorning")==0){
			//printf("NOT Implemented\n");

			//1. Parse minute, hour and songname

			int hour,min;
			sscanf(command->args[1],"%d.%d",&hour,&min);

			char filename[2048];
			strcpy(filename,command->args[2]);

			//printf("Hour: %d\nMinute: %d\nFile: %s\n", hour,min,filename);

			//2. Write crontab command to file

			char FILELOC[128];
			memset(FILELOC,0,128*sizeof(char));
			strcat(FILELOC,getenv("HOME"));
			strcat(FILELOC,alarmfile);

			FILE *fptr = fopen(FILELOC, "w");

		    if (fptr == NULL) {
		        printf("Error! Can't save alarm!");
		        exit(1);
		    }

			fprintf(fptr, "%d %d * * * DISPLAY=:0.0 /usr/bin/rhythmbox-client --play %s\n", min, hour, filename);

			fclose(fptr);

			//3. exec to read crontab

			//$crontab alarm.txt
			//crontab alarm.txt
			char *crontabexec[2] = {"crontab", FILELOC};
			execvp(crontabexec[0], crontabexec);
			exit(0);
		}

		//PART V: kdiff
		else if (strcmp(command->args[0], "kdiff")==0){
			//printf("NOT Implemented\n");
			
			//SWITCH
			//printf("%d\n", command->arg_count);
			int mode = 0;
			if(command->arg_count == 4) mode = 0;
			else if(command->arg_count == 5){
				char c;
				sscanf(command->args[1], "-%c", &c);
				
				mode = ((c=='a')?0:1);
				//printf("Mode: %d\n",mode);
			}
			else{
				printf("E: Incorrect number of arguments for kdiff (2 or 3) \n");
				return EXIT;
			}

			//printf("MODE: %d\n",mode);

			//Check file names

			char filename1[2048], filename2[2048];

			if(command->arg_count == 4){
				strcpy(filename1,command->args[1]);
				strcpy(filename2,command->args[2]);
			}
			else if(command->arg_count == 5){
				strcpy(filename1,command->args[2]);
				strcpy(filename2,command->args[3]);
			}
			else{
				//printf("WE'VE GOT A PROBLEM CHIEF\n");
				return EXIT;
			}
			//Make sure .txt
			//printf("TESTING\n");

			char body[2048],ext[2048];

			char *ptr = strtok(filename1, ".\n");strcpy(body,ptr);ptr = strtok(NULL,".\n");strcpy(ext,ptr);
			//printf("%s . %s \n", body, ext );
			if(strcmp(ext,"txt")!=0) {
				printf("E: File 1 is not a .txt file\n");
				return EXIT;
			}

			ptr = strtok(filename2, ".\n");strcpy(body,ptr);ptr = strtok(NULL,".\n");strcpy(ext,ptr);
			//printf("%s . %s \n", body, ext );
			if(strcmp(ext,"txt")!=0) {
				printf("E: File 2 is not a .txt file\n");
				return EXIT;
			}

			//ADD BACK .txt extension
			strcat(filename1,".txt");
			strcat(filename2,".txt");

			//identical flag
			int identical = 1;

			//first line
			int firstline = 1;

			//file end reached flags
			int f1ended = 0, f2ended=0;

			int linecount = -1, mislinecount=0;

			char * line1 = NULL, *line2 = NULL;
			char byte1, byte2;
		    size_t len = 0;
		    //ssize_t read;

			//PART A (mode = 0)
			//LINE BY LINE
			if(mode==0){

				FILE *f1 = fopen(filename1, "r");
				//FILE *f2 = f1;
				FILE *f2 = fopen(filename2, "r");
			    if ( (f1 == NULL) || (f2 == NULL) ){
			    	//printf("ERROR: %d %d\n", (int)(f1), (int)(f2));
			        exit(EXIT_FAILURE);
			    }

			    while ( !f1ended || !f2ended ) {

			    	linecount++;

			    	if (firstline){
			    		firstline=0;
			    	}
			    	//Compare strings
			    	else if(!f1ended && f2ended){
			    		printf("%s:Line %d: %s\n", filename1,linecount,line1);
			    		mislinecount++;
			    		identical=0;
			    	}
			    	else if(f1ended && !f2ended){
			    		printf("%s:Line %d: %s\n", filename2,linecount,line2);
			    		mislinecount++;
			    		identical=0;
			    	}
			    	else if(strcmp(line1,line2) != 0){
			    		printf("%s:Line %d: %s\n", filename1,linecount,line1);
			    		printf("%s:Line %d: %s\n", filename2,linecount,line2);
			    		mislinecount++;
			    		identical=0;
			    	}

			    	//Read one line from each
			    	if(getline(&line1, &len, f1) == -1){
			    		f1ended=1;
			    	}
			    	if(getline(&line2, &len, f2) == -1){
			    		f2ended=1;
			    	}

			    }

			    //Identical?

			    if(identical){
			    	printf("The two files are identical\n\n");
			    }else{
			    	if(mislinecount==1)
			    		printf("1 different line found\n\n");
			    	else
			    		printf("%d different lines found\n\n", mislinecount);
			    }
			}
			//PART B (mode = 1)
			else{
				FILE *f1 = fopen(filename1, "rb");
				FILE *f2 = fopen(filename2, "rb");
			    if ( (f1 == NULL) || (f2 == NULL) ){
			        exit(EXIT_FAILURE);
			    }

			    while ( !f1ended || !f2ended ) {

			    	//printf("%d\n", linecount);

			    	linecount++;

			    	if (firstline){
			    		firstline=0;
			    	}
			    	//Compare strings
			    	else if(!f1ended && f2ended){
			    		//printf("%s:Byte %d: %c\n", filename1,linecount,byte1);
			    		mislinecount++;
			    		identical=0;
			    	}
			    	else if(f1ended && !f2ended){
			    		//printf("%s:Byte %d: %c\n", filename2,linecount,byte2);
			    		mislinecount++;
			    		identical=0;
			    	}
			    	else if( byte1!=byte2 ){
			    		//printf("%s:Byte %d: %c\n", filename1,linecount,byte1);
			    		//printf("%s:Byte %d: %c\n", filename2,linecount,byte2);
			    		mislinecount++;
			    		identical=0;
			    	}

			    	//Read one line from each
			    	if( (byte1 = fgetc(f1)) == EOF ){
			    		f1ended=1;
			    	}
			    	if( (byte2 = fgetc(f2)) == EOF ){
			    		f2ended=1;
			    	}

			    }

			    //Identical?

			    if(identical){
			    	printf("The two files are identical\n\n");
			    }else{
			    	if(mislinecount==1)
			    		printf("1 different byte found\n\n");
			    	else
			    		printf("%d different bytes found\n\n", mislinecount);
			    }
			}

			return SUCCESS;
		}

		//PART VI: favorite command
		else if (strcmp(command->args[0], "myfavorite")==0){
			//printf("NOT Implemented\n");
			
			//h->commands[0]; h->length;
			
			int checked[HISTORYSIZE], countOf[HISTORYSIZE];
			memset(checked, 0, HISTORYSIZE*sizeof(int));
			memset(countOf, 0, HISTORYSIZE*sizeof(int));
			
			//Step 1: Loop over commands
			//If not checked mark and begin counting
			//If checked continue
			int i,j;
			for(i = 0; i < h->length;i++){
				if(checked[i]) continue;
				//Not checked before, checking now
				countOf[i] = checked[i] = 1;
				for(j = i + 1; j < h->length ;j++){
					if(checked[j]) continue;
					//Not matched before, attempting to match now
					else if(strcmp(h->commands[i],h->commands[j])==0){
						countOf[i] += checked[j] = 1;
					}
				}
			}
			
			//Step 2: Loop over count to find largest count
			int fav=-1, favcount=-1;
			for(i = 0; i < h->length ;i++){
				if(countOf[i] > favcount){
					favcount = countOf[i];
					fav = i;
				}
			}
			
			//Step 3: Return corresponding string with largest count
			printf("Your favorite command lately is %s (%d/%d)\n", h->commands[fav], favcount,h->length);
			
			    /* checked   count
				a 1        2
				b 1        3
				c 1        1
				d 1        1
				b 1        0
				a 1        0
				b 1        0
			    */

			/*printf("\tChecked:\tCount:\tCommand:\n");
			for(i = h->length - 1; i >= 0 ;i--){
				printf("\t%d\t%d\t%s\n", checked[i],countOf[i],h->commands[i]);
			}*/

			return SUCCESS;
		}

		//Non-Builtins
		else
		{
			//execvp(command->name, command->args); // exec+args+path
			//exit(0);
		
		/// TODO: do your own exec with path resolving using execv()
		/// DONE

		char *environ = getenv("PATH");
		char *fileToCheck = environ;
		char split[] = ":";
		char *ptr = strtok(environ, split);

		char exeToCheck[2048];

		while(ptr != NULL) {

		  	//char *exeToCheck = strcat(fileToCheck, command->name);

		  	memset(exeToCheck,0,2048*sizeof(char));
			strcat(exeToCheck,ptr);
			strcat(exeToCheck,"/");
			strcat(exeToCheck,command->name);
		  	//printf("exeToCheck:%s\n", exeToCheck);

		    if(access(exeToCheck, F_OK) == 0){
		       //printf("EXECUTABLE FOUND\n");

		       strcpy(command->args[0], exeToCheck);
			   execv(command->args[0], command->args);

			   break;
			   exit(0);
		    } else {
			  //printf("%s\n", "File does not exist");
		    }
			ptr = strtok(NULL, split);
		}
		printf("%s\n", "E: command not found");
		exit(0);
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

int save_aliases(shortdir *shortdirs){
	//printf("BUG: Make sure aliases.txt and alarm.txt is saved to the root directory (not temp/aliases.txt)\nConsider using getcwd()!\n");

	//SAVE UNDER $HOME
	char FILELOC[128];
	memset(FILELOC,0,128*sizeof(char));
	strcat(FILELOC,getenv("HOME"));
	strcat(FILELOC,aliasfile);

    FILE *fptr = fopen(FILELOC, "w");

    // exiting program 
    if (fptr == NULL) {
        printf("Error! Can't save aliases!");
        exit(1);
    }

    shortdir *s = shortdirs;
	for (; s->next->shortName != NULL ; s=s->next ) {
	    fprintf(fptr, "%s F %s\n", s->shortName, s->longName);
	}

	fclose(fptr);

	return 0;
}
void load_aliases(shortdir *shortdirs){

	//printf("I AM LOADING\n");

	char FILELOC[128];
	memset(FILELOC,0,128*sizeof(char));
	strcat(FILELOC,getenv("HOME"));
	strcat(FILELOC,aliasfile);

	//printf("Loading shortdir aliases from: %s\n", FILELOC);

    shortdir *s = shortdirs;

    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    FILE *f = fopen(FILELOC, "r");
    if (f == NULL){
    	//printf("E: Can't load any aliases file\n, skipping\n");
    	return;
        //exit(EXIT_FAILURE);
    }

    while ((read = getline(&line, &len, f)) != -1) {
        //printf("Retrieved line of length %zu:\n", read);
        //printf("%s", line);

        //seperate alias and longname
		char *token = strtok(line, " F ");
		strcpy(s->shortName,token);
		token = strtok(NULL, " F ");
		token[strcspn(token, "\n")] = 0;
	    strcpy(s->longName,token);

	    //printf("%s : %s\n",s->shortName,s->longName);

	    //RESERVE NEXT ELEMENT
	    s->next=malloc(sizeof(shortdir));
	    memset(s->next, 0, sizeof(shortdir));
	    s->next->prev = s;
	    s=s->next;
    }

    fclose(f);

    if (line)
        free(line);

    //BU BiRAZ DAHA ZOR OLUCAK

	/*for (; s->next->shortName != NULL ; s=s->next ) {
	    fprintf(fptr, "%s F %s\n", s->shortName, s->longName);
	}*/
}

