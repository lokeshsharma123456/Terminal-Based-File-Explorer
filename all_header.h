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
#include"list_dir.cpp"
using namespace std;







string output_permissions(mode_t st);
bool sortcol(const vector<string>& v1, const vector<string>& v2);
void listFiles(const char* dir_name);
void NORMAL_MODE(string);