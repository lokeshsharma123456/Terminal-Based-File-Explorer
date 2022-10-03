// Terminal-Based File Explorer
// Vim-style keyboard-driven file explorer using only POSIX system calls.
// Build: g++ -std=c++17 -O2 -Wall main.cpp -o explorer
//
// Normal mode:
//   Up / Down  -> move cursor
//   Enter      -> open file (xdg-open) / cd into directory
//   Left       -> back   (history stack)
//   Right      -> forward
//   Backspace  -> parent (..)
//   h          -> $HOME
//   :          -> Command mode
//   q          -> quit
//
// Command mode (after ':'):
//   goto <path>
//   create_file <name> <path>
//   create_dir  <name> <path>
//   delete_file <path>
//   delete_dir  <path>          (recursive)
//   copy <src> <dest>
//   move <src> <dest>
//   rename <old> <new>
//   search <name>               (recursive from .)
//   snapshot <path>             (recursive tree)
//   help
//   quit

//----------------------------header files------------
#include <iostream>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <iomanip>
#include <sys/ioctl.h>
#include <stack>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fstream>
#include <fcntl.h>
#include <cmath>
#include <iomanip>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <cstring>
#include <dirent.h>
using namespace std;

// --- ANSI helpers -------------------------------------------------------------

static const char *ANSI_RESET = "\x1b[0m";
static const char *ANSI_DIM = "\x1b[2m";
static const char *ANSI_BOLD = "\x1b[1m";
static const char *ANSI_INV = "\x1b[7m";
static const char *ANSI_CYAN = "\x1b[36m";
static const char *ANSI_YEL = "\x1b[33m";
static const char *ANSI_GRN = "\x1b[32m";
static const char *ANSI_BLU = "\x1b[34m";

static inline void clear_screen() { std::cout << "\x1b[2J\x1b[H"; }
static inline void move_cursor(int row, int col) { std::cout << "\x1b[" << row << ";" << col << "H"; }
static inline void clear_line() { std::cout << "\x1b[2K"; }

// --- Raw mode -----------------------------------------------------------------

static struct termios g_orig_termios;
static bool g_raw_active = false;

static void disable_raw_mode()
{
	if (g_raw_active)
	{
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
		g_raw_active = false;
	}
}

// Restore terminal state on any abnormal termination, then re-raise so
// the OS can produce the normal exit / core dump.
static void on_signal(int sig)
{
	disable_raw_mode();
	// best-effort: clear screen, show cursor, reset attrs
	const char *reset = "\x1b[0m\x1b[?25h\x1b[2J\x1b[H";
	if (write(STDOUT_FILENO, reset, std::strlen(reset)) < 0)
	{ /* ignore */
	}
	std::signal(sig, SIG_DFL);
	std::raise(sig);
}

static void install_signal_handlers()
{
	struct sigaction sa{};
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	for (int s : {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL})
	{
		sigaction(s, &sa, nullptr);
	}
	// ignore SIGPIPE so a closed pager/xdg-open child can't kill us
	std::signal(SIGPIPE, SIG_IGN);
}

static void enable_raw_mode()
{
	if (g_raw_active)
		return;
	tcgetattr(STDIN_FILENO, &g_orig_termios);
	static bool atexit_installed = false;
	if (!atexit_installed)
	{
		std::atexit(disable_raw_mode);
		atexit_installed = true;
	}
	struct termios raw = g_orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON);
	raw.c_iflag &= ~(IXON | ICRNL);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	g_raw_active = true;
}

// --- Path helpers -------------------------------------------------------------

static std::string get_cwd()
{
	char buf[PATH_MAX];
	if (getcwd(buf, sizeof(buf)))
		return std::string(buf);
	return std::string(".");
}

static std::string home_dir()
{
	const char *h = getenv("HOME");
	if (h)
		return std::string(h);
	struct passwd *pw = getpwuid(getuid());
	return pw ? std::string(pw->pw_dir) : std::string("/");
}

// Resolve user-supplied path: expand ~, leave absolute paths alone,
// otherwise treat as relative to `base`.
static std::string resolve_path(const std::string &in, const std::string &base)
{
	if (in.empty())
		return base;
	if (in[0] == '/')
		return in;
	if (in[0] == '~')
		return home_dir() + in.substr(1);
	return base + "/" + in;
}

static bool is_dir(const std::string &path)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
}

// --- Terminal size ------------------------------------------------------------

struct WinSize
{
	int rows = 24, cols = 80;
};
static WinSize term_size()
{
	struct winsize w{};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
	{
		return {(int)w.ws_row, (int)w.ws_col};
	}
	return {};
}

// --- Listing & rendering ------------------------------------------------------

struct Entry
{
	std::string name;
	bool is_directory = false;
	bool stat_ok = false;
	mode_t mode = 0;
	off_t size = 0;
	time_t mtime = 0;
};

//--------------------user defined--------
#define UP 65
#define DOWN 66
#define RIGHT 67
#define LEFT 68
#define ENTER 10
#define MODE 0777
#define BUF_SIZE 8192

//---------global items-----------------

vector<string> vec_dir_list;
int directory_position = 1;
stack<string> back_stack;
stack<string> forward_stack;
string curr_dir;
int LIST_SIZE;
stack<string> st;

//---------stat------------------

static struct termios term, oterm;
struct termios raw;
struct termios raw_out;
struct termios orig_termios;

//----------------FUNCTION PROTOTYPES------------

void keys(string);
void enableRawMode();
void disableRawMode();
void enableRawMode2();
void createdir(string, string);
int search(string, string);
vector<string> getcommand();
void create_file(string, string);
void create_dir(string, string);
void delete_dir(string);
void keys(string);
void copy_file(string, string);
void copy_dir(string, string);
void move_file(string, string);
void move_dir(string, string);
void delete_file(string);
void delete_dir(string);
string name_of_folder(string); // get path from back upto 1st '/'
void rename(string, int, vector<string>);




// One stat per entry, cached on the Entry. Renderer never stats again,
// so we tolerate files vanishing mid-session.
static std::vector<Entry> list_dir(const std::string& path) {
    std::vector<Entry> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (std::strcmp(e->d_name, ".") == 0) continue;
        Entry ent;
        ent.name = e->d_name;
        struct stat st{};
        if (lstat((path + "/" + ent.name).c_str(), &st) == 0) {
            ent.stat_ok = true;
            ent.mode    = st.st_mode;
            ent.size    = st.st_size;
            ent.mtime   = st.st_mtime;
            // follow symlinks for directory classification
            if (S_ISLNK(st.st_mode)) {
                struct stat tgt{};
                if (stat((path + "/" + ent.name).c_str(), &tgt) == 0)
                    ent.is_directory = S_ISDIR(tgt.st_mode);
            } else {
                ent.is_directory = S_ISDIR(st.st_mode);
            }
        }
        out.push_back(std::move(ent));
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.is_directory != b.is_directory) return a.is_directory > b.is_directory;
        return a.name < b.name;
    });
    return out;
}

static std::string perm_string(mode_t m) {
    std::string s;
    s += (S_ISDIR(m) ? 'd' : '-');
    s += (m & S_IRUSR ? 'r' : '-');
    s += (m & S_IWUSR ? 'w' : '-');
    s += (m & S_IXUSR ? 'x' : '-');
    s += (m & S_IRGRP ? 'r' : '-');
    s += (m & S_IWGRP ? 'w' : '-');
    s += (m & S_IXGRP ? 'x' : '-');
    s += (m & S_IROTH ? 'r' : '-');
    s += (m & S_IWOTH ? 'w' : '-');
    s += (m & S_IXOTH ? 'x' : '-');
    return s;
}

static std::string human_size(off_t bytes) {
    static const char* units[] = { "B", "K", "M", "G", "T" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::ostringstream os;
    os << std::fixed << std::setprecision(u == 0 ? 0 : 1) << v << units[u];
    return os.str();
}

static std::string short_time(time_t t) {
    char buf[32];
    struct tm* tm = localtime(&t);
    strftime(buf, sizeof(buf), "%b %d %H:%M", tm);
    return buf;
}

static void render(const std::string& cwd,
                   const std::vector<Entry>& entries,
                   int cursor,
                   int scroll,
                   const std::string& status) {
    WinSize ws = term_size();
    int header_rows = 2;
    int footer_rows = 2;
    int avail = ws.rows - header_rows - footer_rows;
    if (avail < 1) avail = 1;

    clear_screen();

    // Header
    std::cout << ANSI_BOLD << ANSI_CYAN << " <LOKESH /> Terminal File Explorer " << ANSI_RESET
              << ANSI_DIM << "  " << cwd << ANSI_RESET << "\n";
    std::cout << ANSI_DIM
              << " perms       size   modified         name" << ANSI_RESET << "\n";

    // Body — uses cached stat from list_dir, no per-frame syscalls
    int end = std::min((int)entries.size(), scroll + avail);
    for (int i = scroll; i < end; ++i) {
        const Entry& e = entries[i];
        bool selected = (i == cursor);
        if (selected) std::cout << ANSI_INV;

        if (e.stat_ok) {
            std::cout << " " << perm_string(e.mode) << "  "
                      << std::setw(6) << std::right << human_size(e.size) << "  "
                      << short_time(e.mtime) << "   ";
        } else {
            std::cout << " ?????????      ?  ????????????   ";
        }

        if (e.is_directory) std::cout << ANSI_BLU << ANSI_BOLD << e.name << "/" << ANSI_RESET;
        else                std::cout << e.name;

        if (selected) std::cout << ANSI_RESET;
        std::cout << "\n";
    }

    // Pad blank lines
    for (int i = end - scroll; i < avail; ++i) std::cout << "\n";

    // Footer
    std::cout << ANSI_DIM
              << " ↑/↓ move  ↵ open  ← back  → fwd  ⌫ parent  h home  : cmd  q quit"
              << ANSI_RESET << "\n";
    if (!status.empty()) std::cout << ANSI_YEL << " " << status << ANSI_RESET;
    std::cout.flush();
}

// --- File ops -----------------------------------------------------------------

static const size_t BUF_SIZE = 8192;

static int copy_file(const std::string &src, const std::string &dst)
{
	int in = open(src.c_str(), O_RDONLY);
	if (in < 0)
		return -1;
	int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0)
	{
		close(in);
		return -1;
	}
	char buf[BUF_SIZE];
	ssize_t n;
	while ((n = read(in, buf, BUF_SIZE)) > 0)
	{
		ssize_t off = 0;
		while (off < n)
		{
			ssize_t w = write(out, buf + off, n - off);
			if (w <= 0)
			{
				close(in);
				close(out);
				return -1;
			}
			off += w;
		}
	}
	close(in);
	close(out);
	return n < 0 ? -1 : 0;
}

// Iterative copy: pre-order, uses an explicit stack so depth is bounded by
// heap memory, not the call stack. Safe for arbitrarily deep trees.
static int copy_recursive(const std::string &src, const std::string &dst)
{
	struct stat root{};
	if (lstat(src.c_str(), &root) != 0)
		return -1;
	if (!S_ISDIR(root.st_mode))
		return copy_file(src, dst);

	if (mkdir(dst.c_str(), 0755) != 0 && errno != EEXIST)
		return -1;

	std::stack<std::pair<std::string, std::string>> stk;
	stk.push({src, dst});
	int rc = 0;
	while (!stk.empty())
	{
		auto [s, d] = stk.top();
		stk.pop();
		DIR *dir = opendir(s.c_str());
		if (!dir)
		{
			rc = -1;
			continue;
		}
		struct dirent *e;
		while ((e = readdir(dir)) != nullptr)
		{
			std::string n = e->d_name;
			if (n == "." || n == "..")
				continue;
			std::string ss = s + "/" + n;
			std::string dd = d + "/" + n;
			struct stat st{};
			if (lstat(ss.c_str(), &st) != 0)
			{
				rc = -1;
				continue;
			}
			if (S_ISDIR(st.st_mode))
			{
				if (mkdir(dd.c_str(), 0755) != 0 && errno != EEXIST)
				{
					rc = -1;
					continue;
				}
				stk.push({ss, dd});
			}
			else
			{
				if (copy_file(ss, dd) != 0)
					rc = -1;
			}
		}
		closedir(dir);
	}
	return rc;
}

// Iterative delete: collect all paths via DFS, then unlink/rmdir in reverse
// (post-order) so directories are emptied before removal.
static int delete_recursive(const std::string &path)
{
	struct stat root{};
	if (lstat(path.c_str(), &root) != 0)
		return -1;
	if (!S_ISDIR(root.st_mode))
		return unlink(path.c_str());

	std::vector<std::pair<std::string, bool>> all; // (path, is_dir)
	std::stack<std::string> stk;
	stk.push(path);
	while (!stk.empty())
	{
		std::string cur = stk.top();
		stk.pop();
		all.push_back({cur, true});
		DIR *dir = opendir(cur.c_str());
		if (!dir)
			continue;
		struct dirent *e;
		while ((e = readdir(dir)) != nullptr)
		{
			std::string n = e->d_name;
			if (n == "." || n == "..")
				continue;
			std::string sub = cur + "/" + n;
			struct stat st{};
			if (lstat(sub.c_str(), &st) != 0)
				continue;
			if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
				stk.push(sub);
			else
				all.push_back({sub, false});
		}
		closedir(dir);
	}
	int rc = 0;
	for (auto it = all.rbegin(); it != all.rend(); ++it)
	{
		if (it->second)
		{
			if (rmdir(it->first.c_str()) != 0)
				rc = -1;
		}
		else
		{
			if (unlink(it->first.c_str()) != 0)
				rc = -1;
		}
	}
	return rc;
}

// Iterative search.
static bool search_recursive(const std::string &root, const std::string &name)
{
	std::stack<std::string> stk;
	stk.push(root);
	while (!stk.empty())
	{
		std::string cur = stk.top();
		stk.pop();
		DIR *d = opendir(cur.c_str());
		if (!d)
			continue;
		struct dirent *e;
		while ((e = readdir(d)) != nullptr)
		{
			std::string n = e->d_name;
			if (n == "." || n == "..")
				continue;
			if (n == name)
			{
				closedir(d);
				return true;
			}
			std::string sub = cur + "/" + n;
			struct stat st{};
			if (lstat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
				stk.push(sub);
		}
		closedir(d);
	}
	return false;
}

// Iterative snapshot. Children are pushed in reverse order so output is sorted.
static void snapshot_dir(const std::string &path, int /*unused_depth*/, std::ostream &os)
{
	std::stack<std::pair<std::string, int>> stk;
	stk.push({path, 0});
	while (!stk.empty())
	{
		auto [cur, depth] = stk.top();
		stk.pop();
		DIR *d = opendir(cur.c_str());
		if (!d)
			continue;
		std::vector<std::pair<std::string, bool>> kids;
		struct dirent *e;
		while ((e = readdir(d)) != nullptr)
		{
			std::string n = e->d_name;
			if (n == "." || n == "..")
				continue;
			struct stat st{};
			bool d_flag = (lstat((cur + "/" + n).c_str(), &st) == 0) && S_ISDIR(st.st_mode);
			kids.push_back({n, d_flag});
		}
		closedir(d);
		std::sort(kids.begin(), kids.end(),
				  [](const auto &a, const auto &b)
				  { return a.first < b.first; });
		for (const auto &k : kids)
		{
			for (int i = 0; i < depth; ++i)
				os << "  ";
			os << (k.second ? "[d] " : "    ") << k.first << "\n";
		}
		// push in reverse so dirs traverse alphabetically
		for (auto it = kids.rbegin(); it != kids.rend(); ++it)
		{
			if (it->second)
				stk.push({cur + "/" + it->first, depth + 1});
		}
	}
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
				forward_stack.push(s); // current push to forward
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
		else if (ch == 127) // backspace
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
		else if (ch == 104) // home directrotry
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
			printf("\033[0;36m"
				   "COMMAND MODE STARTED:\n ");
			cout << "CURRENT PATH IS :" << curr_dir << endl;
			cout << "$ ";

			while (1)
			{
				char c;
				vector<string> v;
				v = getcommand();

				if (v[0] == "rename")
				{
					if (v.size() != 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
						cout << "WRONG FILE NAME :\n";
						break;
					}
					rename(pwd, a, v);
				}
				else if (v[0] == "27") // to escape
				{

					enableRawMode();
					clear();
					listFiles(curr_dir);
					keys(curr_dir);
				}
				else if (v[0] == "quit")
				{ // write quit then space then enter to quit

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
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
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
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
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
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					string path = check_tilda(v[2]);

					create_file(v[1], path);
					pwd = curr_dir;
					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}
				else if (v[0] == "create_dir") // create_dir
				{

					if (v.size() != 3)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
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
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}
				else if (v[0] == "search") // search file or folder
				{
					if (v.size() != 2)
					{
						clear();
						pwd = curr_dir;
						listFiles(pwd);
						printf("\033[%d;1H", a);
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					int t = search(v[1], ".");
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
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
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
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
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
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					// string s = check_tilda(v[1]);
					// s += "/" + v[1];
					delete_dir(v[1]); // complete path name pr hi kaam karega

					clear();
					listFiles(pwd);
					// keys(pwd);
					printf("\033[%d;1H", a);
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
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
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					for (int i = 1; i <= v.size() - 2; i++)
					{
						string des_path = check_tilda(v[v.size() - 1]);

						if (!isfolder(v[i])) // opposite return true k liye 0
						{
							des_path += "/" + v[i];
							mkdir(des_path.c_str(), 0777);

							string sa = get_current_dir_name();
							string source_foldr = sa + "/" + v[i];

							copy_dir(source_foldr, des_path);
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
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
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
						printf("\033[0;36m"
							   "COMMAND MODE STARTED:\n ");
						cout << "WRONG COMMAND :\n";
						cout << "CURRENT PATH IS :" << curr_dir << endl;
						cout << "$ ";
						break;
					}
					for (int i = 1; i <= v.size() - 2; i++)
					{
						string des_path = check_tilda(v[v.size() - 1]);
						if (!isfolder(des_path)) // opposite return true k liye 0
						{
							string folder_n = curr_dir + "/" + v[i];
							des_path += "/" + v[i];
							move_dir(folder_n, des_path);
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
					printf("\033[0;36m"
						   "COMMAND MODE STARTED:\n ");
					string s = realpath(curr_dir.c_str(), NULL);
					cout << "CURRENT PATH IS :" << s << endl;
					cout << "$ ";
				}
			}
		}
	}
}

void rename(string pwd, int a, vector<string> v)
{
	string s = curr_dir;
	cout << "curr_dir: " << curr_dir << endl;
	string aa = "";
	string b = "";
	aa += s + "/" + v[1];
	b += s + "/" + v[2];
	int result = rename(aa.c_str(), b.c_str());
	clear();
	pwd = curr_dir;
	listFiles(pwd);
	printf("\033[%d;1H", a);
	printf("\033[0;36m"
		   "COMMAND MODE STARTED:\n ");
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
	copy_file(fileName, destination);

	if (remove(fileName.c_str()))
		cout << "fail";
	else
		cout << "success";
	return;
}

void move_dir(string source, string destination)
{

	int result = rename(source.c_str(), destination.c_str());
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
	while (1)
	{
		in = read(src, buf, BUF_SIZE);
		if (in <= 0)
			break;
		out = write(dst, buf, in);
		if (out <= 0)
			break;
	}
	close(src);
	close(dst);
	return;
}

void copy_dir(string folderName, string destination)
{

	DIR *dir = opendir(folderName.c_str());
	if (dir == NULL)
	{
		printf("no such source directory found\n");
		return;
	}
	// cout << folderName << endl;

	struct dirent *entity;
	while ((entity = readdir(dir)) != NULL)
	{
		// printf("%hhd  %s\n", entity->d_type,   entity->d_name);
		if (string(entity->d_name) == "." || string(entity->d_name) == "..")
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
				mkdir(dest_path.c_str(), 0777);
				copy_dir(source_path, dest_path);
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

void create_file(string Name, string folder) // create_file
{
	string s = folder;
	s += "/" + Name;
	FILE *file = fopen(s.c_str(), "w+");
	fclose(file);
}

void create_dir(string Name, string folder)
{

	string s = folder;
	s += "/" + Name;
	mkdir(s.c_str(), 0775);
}

string name_of_folder(string path) // last piche se folder
{
	string a = "";
	for (int i = path.length() - 1; i >= 0; --i)
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

int search(string Name, string folder)
{
	DIR *dir = opendir(folder.c_str());
	if (dir == NULL)
	{
		return 0;
	}
	struct dirent *entity;
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

			char path[100] = {0};
			strcat(path, folder.c_str());
			strcat(path, "/");
			strcat(path, entity->d_name);
			if ((search(Name, path)))
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
	DIR *dir = opendir(dirname.c_str());
	if (dir == NULL)
	{
		return;
	}

	int count = 5;
	// printf("Reading files in: %s\n", dirname);

	struct dirent *entity;
	while ((entity = readdir(dir)) != NULL && (count-- > 0))
	{
		if (entity->d_name == "." || entity->d_name == "..")
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

void enableRawMode2()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

vector<string> getcommand()
{

	char ch;
	string s;

	getline(cin, s);

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
	for (int i = 0; i < s.length(); ++i)
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
	enableRawMode(); // enable to get into canonicall mode
	// /get_current_dir_name()
	curr_dir = get_current_dir_name(); // global path
	back_stack.push(curr_dir);
	listFiles(curr_dir);
	keys(curr_dir);
	return 0;
}
