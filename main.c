// Authors:
// İrem Karaca 71879
// Birkan Çelik 68813

#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>

#define READ_END 0
#define WRITE_END 1

const char *sysname = "shellfyre";

// Global variables for cdh command
char *cdhistory[1000];
int cdh_counter = 0;


enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

// Global variables for ctrlz command
struct command_t *commandz;
char *historyz[1000];
char *cdhistoryz[1000];

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
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
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
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
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

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

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main()
{
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;

		code = process_command(command);
		if (code == EXIT)
			break;

		// free_command(command);
	}

	printf("\n");
	return 0;
}
void file_search_recursive(char *basePath, char *search_word)
{ // search for a file recursively
	char path[1000];
	struct dirent *dp;
	DIR *dir = opendir(basePath);

	// Unable to open directory stream
	if (!dir)
		return;

	while ((dp = readdir(dir)) != NULL)
	{
		if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
		{

			// Construct new path from our base path
			strcpy(path, basePath);
			strcat(path, "/");
			strcat(path, dp->d_name);
			if (strstr(dp->d_name, search_word) != NULL)
			{
				printf("%s\n", path);
			}

			file_search_recursive(path, search_word);
		}
	}

	closedir(dir);
}

void file_open_recursive(char *basePath, char *search_word)
{
	char path[1000];
	struct dirent *dp;
	DIR *dir = opendir(basePath);

	// Unable to open directory stream
	if (!dir)
		return;

	while ((dp = readdir(dir)) != NULL)
	{
		if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0)
		{

			// Construct new path from our base path
			strcpy(path, basePath);
			strcat(path, "/");
			strcat(path, dp->d_name);
			char *new_args[3];
			if (strstr(dp->d_name, search_word) != NULL)
			{
				printf("%s\n", path);
				char exec_arg_zero[1000];
				strcpy(exec_arg_zero, "/bin/xdg-open");
				new_args[0] = "xdg-open";
				new_args[1] = path;
				new_args[2] = NULL;
				const char *path2 = exec_arg_zero;
				pid_t pid = fork();

				if (pid == 0)
				{ // child
					execv(path2, new_args);
				}
				else
				{
					wait(NULL);
				}
			}

			file_open_recursive(path, search_word);
		}
	}

	closedir(dir);
}

void file_search(char **args, int argCount)
/**
 * @brief The command takes an input keyword
and scans all the files in the current directory to match the keyword with the file name and
then it returns the list of file(s) with this name regardless of the file extensions
 * 
 */
{

	if (argCount == 1)
	{
		// normal search
		DIR *d;
		struct dirent *dir;
		char *dir_array[1024];
		d = opendir(".");
		int count = 0;
		if (d)
		{
			while ((dir = readdir(d)) != NULL)
			{
				dir_array[count] = malloc(1024);
				strcpy(dir_array[count], dir->d_name);
				count++;
			}
			closedir(d);
		}
		for (int i = 0; i < count; i++)
		{
			if (strstr(dir_array[i], args[0]) != NULL)
			{
				printf("%s\n", dir_array[i]);
			}
		}
	}
	else if (argCount == 2 && strcmp(args[0], "-r") == 0)
	{
		// search for file recursively
		file_search_recursive(".", args[1]);
	}
	else if (argCount == 2 && strcmp(args[0], "-o") == 0)
	{
		// search for file and open
		DIR *d;
		struct dirent *dir;
		char *dir_array[1024];
		d = opendir(".");
		int count = 0;
		if (d)
		{
			while ((dir = readdir(d)) != NULL)
			{
				dir_array[count] = malloc(1024);
				strcpy(dir_array[count], dir->d_name);
				count++;
			}
			closedir(d);
		}
		char *new_args[3];
		for (int i = 0; i < count; i++)
		{
			if (strstr(dir_array[i], args[1]) != NULL)
			{
				printf("%s\n", dir_array[i]);
				char exec_arg_zero[1000];

				strcpy(exec_arg_zero, "/bin/xdg-open");
				new_args[0] = "xdg-open";
				new_args[1] = dir_array[i];
				new_args[2] = NULL;
				const char *path = exec_arg_zero;
				pid_t pid = fork();

				if (pid == 0)
				{ // child
					execv(path, new_args);
				}
				else
				{
					wait(NULL);
				}
			}
		}
	}
	else if ((argCount == 3 && strcmp(args[0], "-o") == 0 && strcmp(args[1], "-r") == 0) || (argCount == 3 && strcmp(args[0], "-r") == 0 && strcmp(args[1], "-o") == 0))
	{
		// search for file recursively and open
		file_open_recursive(".", args[2]);
	}
	else
	{
		printf("Invalid arguments\n");
	}
}

int process_command(struct command_t *command)
{
	int r;
	// put the previous command to the commandz variable in case ctrlz is called after that
	if(strcmp(command->name, "ctrlz")!=0){
		commandz = command;
	}
	

	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0)
		return EXIT;

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			// put the previous path to the historyz variable in case ctrlz is called after that
			strcpy(historyz, getcwd(NULL, 0));
			// put the all elements in cdhistory to the cdhistoryz one by one in case ctrlz is called after that
			for (int i = 0; i < cdh_counter; i++)
			{
				cdhistoryz[i] = malloc(1000);
				strcpy(cdhistoryz[i], cdhistory[i]);
			}


			r = chdir(command->args[0]);
			if (r == -1)
			{
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			}
			else
			{	
				// cdh_counter can remember max 10 prev. cd commands.
				if (cdh_counter < 10)
				{
					cdhistory[cdh_counter] = malloc(1000);
					strcpy(cdhistory[cdh_counter], getcwd(NULL, 0));
					cdh_counter++;
				}
				else
				{
					// if user makes more than 10 cd commands cdh go and change first command it came.
					for (int i = 0; i < 9; i++)
					{
						strcpy(cdhistory[i], cdhistory[i + 1]);
					}

					for (int i = 0; i < 9; i++)
					{
						strcpy(cdhistory[i], cdhistory[i + 1]);
					}
					cdhistory[9] = malloc(1000);
					//getcwd commands takes current directory we are in.
					strcpy(cdhistory[9], getcwd(NULL, 0));
				}
			}

			return SUCCESS;
		}
	}
	
	if (strcmp(command->name, "cdh") == 0)
	{
		// this command stores 10 cd commands before. 
		if (cdh_counter == 0)
		{
			printf("No previous directories to select. Please cd at least once :)\n");
			return SUCCESS;
		}
		for (int i = 0; i < cdh_counter - 1; i++)
		{
			printf("%c\t%d) %s\n", cdh_counter - i - 1 + 96, cdh_counter - i - 1, cdhistory[i]);

		}

		// after that user choose a directory to continue 
		int num;
		char str[50];
		printf("Select directory by letter or number: ");
		gets(str);
		// user's choice is evaluated using ASCI properties
		if(str[0] > 96 && str[0] < 107){
			num = str[0] - 96;
		}
		else{
			num = atoi(str);
		}
		
	
		if (num > 0 && num < cdh_counter)
		{
			chdir(cdhistory[cdh_counter - num - 1]);
			return SUCCESS;
		}

		if (num - 96 > 0 && num - 96 < cdh_counter)
		{
			chdir(cdhistory[cdh_counter - num + 96 - 1]);
			return SUCCESS;
		}
	}

	if (strcmp(command->name, "filesearch") == 0)
	{
		file_search(command->args, command->arg_count);

		return SUCCESS;
	}

	if (strcmp(command->name, "take") == 0)
	{
		char *str = command->args[0];
		const char s[2] = "/";
		char *token;
		token = strtok(str, s);
		while (token != NULL)
		{
			// give required permissons to the file
			mkdir(token, 0777);
			r = chdir(token);
			if (r == -1)
			{
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			}
			else
			{
				if (cdh_counter < 10)
				{
					cdhistory[cdh_counter] = malloc(1000);
					strcpy(cdhistory[cdh_counter], getcwd(NULL, 0));
					cdh_counter++;
					
				}
				else
				{	
					//shifting all elements in cdhistory by one
					for (int i = 0; i < 9; i++)
					{
						strcpy(cdhistory[i], cdhistory[i + 1]);
					}
					cdhistory[9] = malloc(1000);
					strcpy(cdhistory[9], getcwd(NULL, 0));
				}
			}
			token = strtok(NULL, s);
		}

		return SUCCESS;
	}

	if (strcmp(command->name, "joker") == 0)
	{

		char command[1000], msg[100], command2[100], msg2[500];
		// this command is used to send a message to a user using crotab file.
		// It writes command to crontab file to execute a commands that is throwing a joke every 15 minutes.
		// curl commnds get the joke from the website. $ sign executes that particular command in crontab file.

		strcpy(command, "crontab -l | { cat; echo \"*/15 * * * * XDG_RUNTIME_DIR=/run/user/$(id -u) /usr/bin/notify-send \\\"\\$(curl -s https://icanhazdadjoke.com)\\\"\";} | crontab -");
		system(command);
		return SUCCESS;
	}

	// ctrlz takes back the last command executed
	if(strcmp(command->name, "ctrlz")==0){
		if(strcmp(commandz->name, "cd")==0){
			chdir(historyz);
			cdh_counter--;
			for (int i = 0; i < cdh_counter; i++)
			{
				strcpy(cdhistory[i], cdhistoryz[i]);
			}
		}
		if(strcmp(commandz->name, "mkdir")==0){
			rmdir(commandz->args[0]);
		}
	}

	// this command prints a poem into terminal
	// it first creates a random number to choose a poem from poems folder 
	// then it reads the poem from the file and prints it to terminal
	if (strcmp(command->name, "poet") == 0)
	{
		int randomnumber;
		size_t len = 0;
		char *file_name = malloc(1000);
		char *s;
		char textNumber[200];

		FILE *f;

		srand(time(NULL));
		randomnumber = rand() % 10;

		sprintf(textNumber, "%d", randomnumber);
		strcat(file_name, "poems/");
		strcat(file_name, textNumber);
		strcat(file_name, ".txt");
		f = fopen(file_name, "r");

		if (!f)
		{
			printf("Could not open the file");
		}

		while (getline(&s, &len, f) != -1)
		{
			printf("%s", s);
		}
		printf("\n");
		fclose(f);

		free(file_name);
		return SUCCESS;
	}
	// TODO: Implement your custom commands here

	pid_t pid = fork();

	if (pid == 0) // child
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		/// TODO: do your own exec with path resolving using execv()
		// Generate the path to use in execv, then give it to execv as an argument
		// So that commands under the bin directory can be executed
		char exec_arg_zero[1000];
		strcpy(exec_arg_zero, "/bin/");
		strcat(exec_arg_zero, command->args[0]);
		const char *path = exec_arg_zero; // exec_arg_zero;
		execv(path, command->args);

		exit(0);
	}
	else
	{
		/// TODO: Wait for child to finish if command is not running in background

		// print_command(command);
		if (command->background == false) // check if BOOLEAN comparison can be done this way or not.
		{
			wait(NULL);
		}
		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
