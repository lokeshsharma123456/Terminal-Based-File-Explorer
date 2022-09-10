//----------------------------header files------------
#include<iostream>
#include<stdio.h>
#include<string>
#include<string.h>
#include<vector>
#include<stdlib.h>
#include<termios.h>
#include<unistd.h>
#include<ctype.h>
#include<limits.h>
#include<dirent.h>
#include<sys/stat.h>
#include<algorithm>
#include<iomanip>
#include<sys/ioctl.h>
#include<stack>
#include<pwd.h>
#include<grp.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<fstream>
#include<fcntl.h>
#include <cmath>
#include<iomanip>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include<cstring>
#include<dirent.h>
using namespace std;

//--------------------user defined--------
#define UP 65
#define DOWN 66
#define RIGHT 67
#define LEFT 68
#define ENTER 10
#define MODE 0777
#define BUF_SIZE 8192


//---------global items-----------------

vector<string>vec_dir_list;
int directory_position = 1;
stack<string>back_stack;
stack<string>forward_stack;
string curr_dir ;
int LIST_SIZE ;
stack <string>st;



//---------stat------------------

static struct termios term, oterm;
struct termios raw;
struct termios raw_out;
struct termios orig_termios;


//----------------FUNCTION PROTOTYPES------------

void keys(string  );
void enableRawMode() ;
void disableRawMode();
void enableRawMode2() ;
void createdir(string , string);
int search(string , string);
vector<string> getcommand();
void create_file(string , string);
void create_dir(string , string);
void delete_dir(string );
void keys(string);
void copy_file(string , string );
void copy_dir(string , string );
void move_file(string , string );
void move_dir(string , string );
void delete_file( string );
void delete_dir(string );
string name_of_folder(string );//get path from back upto 1st '/'
void rename(string, int , vector<string>);

//-----------------RAW MODE ----------------
void disableRawMode();
void enableRawMode();
void editorRefreshScreen()
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
}

string cwd()
{
	// string a = get_current_dir_name();
	// return a;
	char cwd[100];
	getcwd(cwd, sizeof(cwd));
	string a = cwd;
	return a;
}
string prnt = cwd();

int isfolder(string fileName)
{
	struct stat path;

	stat(fileName.c_str(), &path);

	return S_ISREG(path.st_mode);
}


int isDir(const char* fileName)
{
	struct stat path;

	stat(fileName, &path);

	return S_ISREG(path.st_mode);
}

void clear()
{
	cout << "\033[2J\033[1;1H";
	// printf(" \033[%d;%dH", 4, 0);
}

string output_permissions(mode_t st)
{
	string s = "";

	s += ( st & S_IRUSR ? "r" : "-" );
	s += ( st & S_IWUSR ? "w" : "-" );
	s += ( st & S_IXUSR ? "x" : "-" );
	s += ( st & S_IRGRP ? "r" : "-" );
	s += ( st & S_IWGRP ? "w" : "-" );
	s += ( st & S_IXGRP ? "x" : "-" );
	s += ( st & S_IROTH ? "r" : "-" );
	s += ( st & S_IWOTH ? "w" : "-" );
	s += ( st & S_IXOTH ? "x" : "-" );
	return s;
}
string check_tilda(string s)
{
	string t = "";
	t += s[0];
	if (t == "~")
	{
		t = s.substr(1, s.length() - 1);
		const char *homedir;
		if ((homedir = getenv("HOME")) == NULL)
		{
			homedir = getpwuid(getuid())->pw_dir;
		}
		t = homedir + t;
		return t;
	}
	else if (t == "/")
	{
		return s;
	}
	return "";
}


//---PRINT DATA OF A PARTICULAR FILE
void print ()
{
	struct group *grp;
	struct passwd *pwd;
	struct stat st;
	for (int i = 0; i < vec_dir_list.size(); i++)
	{
		stat(vec_dir_list[i].c_str(), &st);
		string perm = output_permissions(st.st_mode);

		cout << setw(2) << perm << "\t";

		//2.links
		string link =  to_string(st.st_nlink);
		cout << setw(2) << link << "\t";

		//3.group id
		grp = getgrgid(st.st_uid );
		//printf(" %s", grp->gr_name);
		//temp.push_back(grp->gr_name);
		cout << setw(2) << grp->gr_name << "\t";

		//4.person id
		pwd = getpwuid(st.st_gid);
		//printf(" %s", pwd->pw_name);
		cout << setw(2) << pwd->pw_name << "\t  ";

		//5.size
		int size = st.st_size;
		float a = size / 1024.0;
		cout << setw(6) << fixed << setprecision(2) << a << " KB\t  ";


		//6.modified time
		string time = ctime(&st.st_mtime) ;
		string t2 = time.substr(0, time.length() - 1);
		cout << setw(2) << t2 << "\t";

		cout << left << setw(10) << vec_dir_list[i];
		cout << endl;
	}
}

//-------------Push all files of Current dir to vector------------------
void listFiles(string dir_name)
{
	clear();

	vec_dir_list.clear();
	DIR * dir = opendir(dir_name.c_str());
	if (dir == NULL)
		return ;

	dirent *entity;
	entity = readdir(dir);

	struct stat st;

	while ( entity != NULL)
	{
		stat(entity->d_name, &st);
		vec_dir_list.push_back(entity->d_name);
		entity = readdir(dir);
	}

	//Sort on basis of name
	sort(vec_dir_list.begin(), vec_dir_list.end());
	// sort(file_name.begin(), file_name.end());

	closedir(dir);
	print();
}



//------------********keys -*****-(most important)--------------

void keys(string pwd)
{
	LIST_SIZE = vec_dir_list.size();
	cout << LIST_SIZE << endl;
	int pos = 0;
	char ch;
	curr_dir = cwd();
	printf(" \e[H");
	while (1)
	{
		ch = cin.get();
		if (ch == DOWN)
		{
			if (pos < LIST_SIZE)
			{

				pos++;
				directory_position = pos;

				printf("%c[%d;%df", 0x1B, pos, 1);
				// cout << pos;
			}
		}
		else if (ch == UP)
		{
			if (pos >= 0)
			{
				pos--;
				directory_position = pos;


				printf("%c[%d;%df", 0x1B, pos, 1);
				// cout << pos;
			}
		}
		else if (ch == ENTER)
		{
			struct stat st;


			string s = pwd;
			s += "/" + vec_dir_list[directory_position - 1];

			stat(s.c_str(), &st);


			if ((st.st_mode & S_IFMT) == S_IFDIR)
			{

				back_stack.push(pwd);
				pwd = s;
				curr_dir = pwd;
				cout << "curr_dir :" << curr_dir << endl;
				clear();
				vec_dir_list.clear();
				// cout << s << "-----------------" << endl;
				directory_position = 1;
				listFiles(s);
				keys(s);
			}
			else if ((st.st_mode & S_IFMT) == S_IFREG)
			{
				pid_t pid = fork();
				if (pid == 0)
				{
					execlp("xdg-open", "xdg-open", s.c_str(), NULL);
					exit(0);
				}

			}


		}
		else if (ch == LEFT)
		{
			if (!back_stack.empty())
			{
				clear();
				string s = back_stack.top();
				forward_stack.push(s);//current push to forward
				back_stack.pop();
				listFiles(s);
				keys(s);

			}
		}
		else if (ch == RIGHT)
		{

			if (!forward_stack.empty())
			{
				clear();
				string s = forward_stack.top();
				forward_stack.pop();
				back_stack.push(pwd);
				listFiles(s);
				keys(s);
			}
		}
		else if ( ch ==  127) //backspace
		{
			clear();

			// int i;
			// for (i = prnt.length() - 1; prnt[i] != '/' && i >= 0; i--);
			// prnt = prnt.substr(0, i);
			back_stack.push(curr_dir);
			// pwd = prnt;
			curr_dir += "/";
			curr_dir += "..";
			listFiles(curr_dir);
		}
		else if (ch == 104) //home directrotry
		{
			clear();
			const char *homedir;

			if ((homedir = getenv("HOME")) == NULL)
			{
				homedir = getpwuid(getuid())->pw_dir;
			}
			back_stack.push(curr_dir);
			pwd = string(homedir);

			curr_dir = string(homedir);
			listFiles(pwd);
			keys(pwd);
		}
		else if (ch == 'q')
		{
			printf("\033c");
			return;
		}
		else if (ch == ':')
		{
			// clear();
			disableRawMode();
			enableRawMode2();
			// editorRefreshScreen();
			// cout << "YOU ARE IN COMMAND MODE : \n";
			// char c = cin.get();

			struct winsize w;
			ioctl(0, TIOCGWINSZ, &w);
			int rows = w.ws_row;
			int a = rows - 12;


			printf("\033[%d;1H", a);
			printf("\033[0;36m""COMMAND MODE STARTED:\n ");
			cout << "CURRENT PATH IS :" << curr_dir << endl;
			cout << "$ ";

			while (1)
			{
				char c;
				vector <string> v;
				v = getcommand();

				if (v[0] == "rename")
				{
					if (v.size() != 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG FILE NAME :\n";
						break;
					}
					rename(pwd, a, v);
				}
				else if (v[0] == "27")//to escape
				{

					enableRawMode();
					clear();
					listFiles(curr_dir);
					keys(curr_dir);
				}
				else if (v[0] == "quit")
				{	//write quit then space then enter to quit

					printf("\033c");
					return;
				}
				else if (v[0] == "goto")
				{

					if (v.size() != 2)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}

					curr_dir = check_tilda(v[1]);
					pwd = curr_dir;
					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}
				else if (v[0] == "create_file")
				{
					if (v.size() != 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					string path = check_tilda(v[2]);

					create_file(v[1] , path);
					pwd = curr_dir;
					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}
				else if ( v[0] == "create_dir")//create_dir
				{

					if (v.size() != 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					string path = check_tilda(v[2]);
					create_dir(v[1], path);
					pwd = curr_dir;
					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}
				else if (v[0] == "search") //search file or folder
				{
					if (v.size() != 2)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					int t = search(v[1] , ".");
					if (t == 1)
						cout << "TRUE";
					else
						cout << "FALSE";
				}
				else if (v[0] == "delete_file")
				{
					if (v.size() != 2)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					string s = check_tilda(v[1]);
					s += "/" + v[1];
					if (remove(v[1].c_str()))
						cout << "fail";
					else
						cout << "success";

					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string st = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << st << endl;
					cout << "$ ";
				}
				else if (v[0] == "delete_dir")
				{
					if (v.size() != 2)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					// string s = check_tilda(v[1]);
					// s += "/" + v[1];
					delete_dir(v[1]);//complete path name pr hi kaam karega


					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string st = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << st << endl;
					cout << "$ ";
				}
				else if (v[0] == "copy")
				{
					if (v.size() < 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					for (int i = 1; i <= v.size() - 2; i++)
					{
						string des_path = check_tilda(v[v.size() - 1]);
						
						
						
						if (!isfolder(v[i])) //opposite return true k liye 0
						{
							des_path += "/" + v[i];
							mkdir( des_path.c_str(), 0777 );

							string sa = get_current_dir_name();
							string source_foldr = sa + "/" + v[i];

							copy_dir(source_foldr, des_path );
						}
						else
						{
							string s = curr_dir + "/" + v[i];
							cout << "\n\n\n\n\n\n\n:::::: " << curr_dir << "  \n:::\n\n\n\n\n";
							copy_file(s, des_path);
						}
					}
					pwd = curr_dir;
					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";

				}
				else if (v[0] == "move")
				{
					if (v.size() < 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m""COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					for (int i = 1; i <= v.size() - 2; i++)
					{
						string des_path = check_tilda(v[v.size() - 1]);
						if (!isfolder(des_path)) //opposite return true k liye 0
						{
							string folder_n = curr_dir + "/" + v[i];
							des_path += "/" + v[i];
							move_dir(folder_n , des_path );
						}
						else
						{
							string s = curr_dir + "/" + v[i];
							move_file(s, des_path);
						}
					}
					pwd = curr_dir;
					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m""COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}

			}
		}

	}
}

void rename(string pwd, int a, vector<string>v)
{
	string s = curr_dir;
	cout << "curr_dir: " << curr_dir << endl;
	string aa = "";
	string b = "";
	aa += s + "/" + v[1];
	b += s + "/" + v[2];
	int result = rename(aa.c_str() , b.c_str() );
	clear();
	pwd = curr_dir;
	listFiles(pwd);
	printf("\033[%d;1H", a);
	printf("\033[0;36m""COMMAND MODE STARTED:\n ");
	string ss = realpath(curr_dir.c_str(), NULL);
	if (result == 0)
		cout << "successful rename\n";
	else
		cout << "Write file name carefully\n";
	cout << "CURRENT PATH IS :" << ss << endl;
	cout << "$ ";
}
void move_file(string fileName, string destination)
{
	copy_file(fileName , destination);


	if (remove(fileName.c_str()))
		cout << "fail";
	else
		cout << "success";
	return;
}

void move_dir(string source , string destination)
{

	int result = rename(source.c_str() , destination.c_str() );
	if (result == 0)
		cout << "success";
	else
		cout << "something went wrong";
}

void copy_file(string sr, string dt)
{


  int src, dst, in, out;
  char buf[BUF_SIZE];
  // if (argc != 3) exit(1);

  src = open(sr.c_str(), O_RDONLY);
  if (src < 0)
    exit(2);
  // dt += "/" + sr;//complete path +name
  dst = creat(dt.c_str(), MODE);
  if (dst < 0)
    exit(3);
  while (1) {
    in = read(src, buf, BUF_SIZE);
    if (in <= 0) break;
    out = write(dst, buf, in);
    if (out <= 0) break;
  }
  close(src); close(dst); return;
}



void copy_dir(string folderName, string destination)
{

	DIR* dir = opendir(folderName.c_str());
	if (dir == NULL)
	{
		printf("no such source directory found\n");
		return;
	}
	// cout << folderName << endl;

	struct dirent* entity;
	while ((entity = readdir(dir)) != NULL )
	{
		// printf("%hhd  %s\n", entity->d_type,   entity->d_name);
		if ( string(entity->d_name) == "." || string(entity->d_name) == "..")
		{
			// cout << string(entity->d_name) << endl;
			continue;
		}
		else
		{
			string s = string(entity->d_name);
			string source_path = folderName + "/" + entity->d_name;
			struct stat tmp;
			if (stat(source_path.c_str(), &tmp) == -1)
			{
				printf(" cannot get souurce  stat\n");
				continue;
			}
			if (!isfolder(source_path))
			{
				string dest_path = destination + "/" + entity->d_name;
				mkdir( dest_path.c_str(), 0777 );
				copy_dir(source_path, dest_path );
			}
			else
			{
				string dest_path = destination + "/" + entity->d_name;
				copy_file(source_path, dest_path);
			}

			// cout << "entity name: " << s << endl;
			// cout << "source_path:" << source_path << endl;
			// cout << "destination_path" << dest_path << endl;
		}


	}
	closedir(dir);

	return;
}


void create_file(string  Name , string folder)//create_file
{
	string s = folder;
	s += "/" + Name;
	FILE* file = fopen(s.c_str(), "w+");
	fclose(file);

}


void create_dir(string Name , string folder)
{

	string s = folder;
	s += "/" + Name;
	mkdir( s.c_str(), 0775 );
}


string name_of_folder(string path)//last piche se folder 
{
	string a = "";
	for (int i = path.length() - 1; i >= 0 ; -- i)
	{
		string m = "";
		m += path[i];
		if (m != "/")
			a += m;
		else
			break;
	}

	path = "";
	for (int i = a.length() - 1; i >= 0; i--)
	{
		path += a[i];
	}
	return path;

}


int  search(string Name , string folder)
{
	DIR* dir = opendir(folder.c_str());
	if (dir == NULL) {
		return 0;
	}
	struct dirent* entity;
	entity = readdir(dir);
	while (entity != NULL)
	{
		if (entity->d_type == DT_DIR && strcmp(entity->d_name, ".") != 0 && strcmp(entity->d_name, "..") != 0)
		{
			string folder_name = name_of_folder(folder);
			if (Name == entity->d_name)
			{
				return 1;
			}


			char path[100] = { 0 };
			strcat(path, folder.c_str());
			strcat(path, "/");
			strcat(path, entity->d_name);
			if ((search(Name , path)))
				return 1;
		}
		else
		{
			if (entity->d_name == Name)
			{
				return 1;
			}
		}
		entity = readdir(dir);
	}
	closedir(dir);
	return 0;
}

void delete_dir(string dirname)
{
	DIR* dir = opendir(dirname.c_str());
	if (dir == NULL) {
		return;
	}

	int count = 5;
	// printf("Reading files in: %s\n", dirname);

	struct dirent* entity;
	while ((entity = readdir(dir)) != NULL && (count-- > 0))
	{
		if (entity->d_name == "."  || entity->d_name == "..")
			continue;
		else if (entity->d_type == DT_DIR && strcmp(entity->d_name, ".") != 0 && strcmp(entity->d_name, "..") != 0)
		{
			string path = dirname + "/" + entity->d_name;
			// cout << "foldr_name: " << entity->d_name << endl;
			delete_dir(path);
		}
		else
		{
			string path = dirname + "/" + entity->d_name;
			// cout << "\n--fiile:  " << entity->d_name << endl;
			// cout << "file_path: " << path << endl;
			if (strcmp(entity->d_name, ".") != 0 && strcmp(entity->d_name, "..") != 0)
			{
				int status = remove(path.c_str());
				if (status == 0)
					cout << "\nFile Deleted Successfully!\n";
				else
					cout << "\nIN FILE Error Occurred!\n";
			}
		}

	}
	closedir(dir);

	// string path = dirname + "/" + entity->d_name;
	// cout << "foldr_name: " << dirname << endl;
	int status = remove(dirname.c_str());

	if (status == 0)
		cout << "\nFOLDER Deleted Successfully!\n";
	else
		cout << "\nIN FOLDER Error Occurred!\n";
	return;
}










void enableRawMode2() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


void enableRawMode() {
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

vector<string> getcommand()
{

	char ch;
	string s ;

	getline (cin , s);

	//-------------------------------------------------
	// string s = "";
	// char c;
	// c = cin.get();
	// while (c != 10)
	// {
	// 	if (c != 127)
	// 	{
	// 		s += c;
	// 	}
	// 	else if (c == 127)
	// 	{
	// 		printf ("\033[0K");
	// 		s = s.substr(0, s.length() - 1);
	// 		cout << s;
	// 	}
	// 	c = cin.get();
	// }
	//---------------------------------------------------------
	vector<string> v;
	string input = "";
	for (int i = 0; i < s.length() ; ++i)
	{
		string m = "";
		m += s[i];
		if (m != " ")
		{
			input += m;

		}
		else if (m == " ")
		{
			v.push_back(input);
			input = "";
		}


	}
	v.push_back(input);
	return v;

}



int main()
{
	clear();
	enableRawMode();//enable to get into canonicall mode
	// /get_current_dir_name()
	curr_dir = get_current_dir_name(); //global path
	back_stack.push(curr_dir);
	listFiles(curr_dir);
	keys(curr_dir);
	return 0;
}

