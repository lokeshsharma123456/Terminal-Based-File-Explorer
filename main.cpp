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
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
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


void keys(string);
void enableRawMode();
void disableRawMode();
void enableRawMode2();
void move_dir(string, string);
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
// --- Command mode -------------------------------------------------------------

// Shell-like tokenizer:
//   * single and double quotes preserve spaces inside
//   * backslash escapes the next character (inside or outside quotes)
//     e.g. create_file "my file.txt" /home   or   move my\ file.txt /tmp
static std::vector<std::string> tokenize(const std::string &s)
{
	std::vector<std::string> out;
	std::string cur;
	bool in_token = false;
	char quote = 0;
	for (size_t i = 0; i < s.size(); ++i)
	{
		char c = s[i];
		if (quote)
		{
			if (c == '\\' && i + 1 < s.size())
			{
				cur += s[++i];
				continue;
			}
			if (c == quote)
			{
				quote = 0;
				continue;
			}
			cur += c;
		}
		else if (c == '"' || c == '\'')
		{
			quote = c;
			in_token = true;
		}
		else if (c == '\\' && i + 1 < s.size())
		{
			cur += s[++i];
			in_token = true;
		}
		else if (c == ' ' || c == '\t')
		{
			if (in_token)
			{
				out.push_back(cur);
				cur.clear();
				in_token = false;
			}
		}
		else
		{
			cur += c;
			in_token = true;
		}
	}
	if (in_token)
		out.push_back(cur);
	return out;
}

static std::string err_msg(const std::string &prefix)
{
	return prefix + ": " + std::strerror(errno);
}

// Returns a status message for the next render. Sets `quit` if user asked to exit.
// Sets `new_cwd` if the user goto'd somewhere.
static std::string run_command(const std::vector<std::string> &v,
							   const std::string &cwd,
							   std::string &new_cwd,
							   bool &quit)
{
	if (v.empty())
		return "";
	const std::string &cmd = v[0];

	auto need = [&](size_t n) -> bool
	{
		return v.size() == n;
	};

	if (cmd == "quit" || cmd == "exit")
	{
		quit = true;
		return "bye.";
	}
	if (cmd == "help")
	{
		return "goto|create_file|create_dir|delete_file|delete_dir|copy|move|rename|search|snapshot|quit";
	}

	if (cmd == "goto")
	{
		if (!need(2))
			return "usage: goto <path>";
		std::string p = resolve_path(v[1], cwd);
		if (!is_dir(p))
			return "not a directory: " + p;
		new_cwd = p;
		return "moved to " + p;
	}
	if (cmd == "create_file")
	{
		if (!need(3))
			return "usage: create_file <name> <path>";
		std::string dir = resolve_path(v[2], cwd);
		if (!is_dir(dir))
			return "not a directory: " + dir;
		if (access(dir.c_str(), W_OK) != 0)
			return err_msg("no write permission on " + dir);
		std::string full = dir + "/" + v[1];
		std::ofstream f(full);
		return f ? "created " + full : err_msg("create_file " + full);
	}
	if (cmd == "create_dir")
	{
		if (!need(3))
			return "usage: create_dir <name> <path>";
		std::string dir = resolve_path(v[2], cwd);
		if (!is_dir(dir))
			return "not a directory: " + dir;
		if (access(dir.c_str(), W_OK) != 0)
			return err_msg("no write permission on " + dir);
		std::string full = dir + "/" + v[1];
		return mkdir(full.c_str(), 0755) == 0 ? "created " + full : err_msg("create_dir " + full);
	}
	if (cmd == "delete_file")
	{
		if (!need(2))
			return "usage: delete_file <path>";
		std::string p = resolve_path(v[1], cwd);
		return unlink(p.c_str()) == 0 ? "deleted " + p : err_msg("delete_file " + p);
	}
	if (cmd == "delete_dir")
	{
		if (!need(2))
			return "usage: delete_dir <path>";
		std::string p = resolve_path(v[1], cwd);
		return delete_recursive(p) == 0 ? "deleted " + p : err_msg("delete_dir " + p);
	}
	if (cmd == "copy")
	{
		if (!need(3))
			return "usage: copy <src> <dest>";
		std::string s = resolve_path(v[1], cwd);
		std::string d = resolve_path(v[2], cwd);
		if (access(s.c_str(), R_OK) != 0)
			return err_msg("cannot read " + s);
		if (is_dir(d))
		{
			std::string base = v[1];
			auto pos = base.find_last_of('/');
			if (pos != std::string::npos)
				base = base.substr(pos + 1);
			d += "/" + base;
		}
		return copy_recursive(s, d) == 0 ? "copied " + s + " -> " + d
										 : err_msg("copy " + s);
	}
	if (cmd == "move")
	{
		if (!need(3))
			return "usage: move <src> <dest>";
		std::string s = resolve_path(v[1], cwd);
		std::string d = resolve_path(v[2], cwd);
		if (is_dir(d))
		{
			std::string base = v[1];
			auto pos = base.find_last_of('/');
			if (pos != std::string::npos)
				base = base.substr(pos + 1);
			d += "/" + base;
		}
		if (rename(s.c_str(), d.c_str()) == 0)
			return "moved " + s + " -> " + d;
		if (errno != EXDEV)
			return err_msg("move " + s);
		// cross-device fallback
		if (copy_recursive(s, d) != 0)
			return err_msg("move (copy phase) " + s);
		if (delete_recursive(s) != 0)
			return err_msg("move (delete phase) " + s);
		return "moved " + s + " -> " + d;
	}
	if (cmd == "rename")
	{
		if (!need(3))
			return "usage: rename <old> <new>";
		std::string a = resolve_path(v[1], cwd);
		std::string b = resolve_path(v[2], cwd);
		return rename(a.c_str(), b.c_str()) == 0 ? "renamed" : err_msg("rename");
	}
	if (cmd == "search")
	{
		if (!need(2))
			return "usage: search <name>";
		return search_recursive(cwd, v[1]) ? "found: " + v[1] : "not found: " + v[1];
	}
	if (cmd == "snapshot")
	{
		if (!need(2))
			return "usage: snapshot <path>";
		std::string p = resolve_path(v[1], cwd);
		if (!is_dir(p))
			return "not a directory: " + p;
		std::cout << "\n"
				  << ANSI_BOLD << "snapshot of " << p << ANSI_RESET << "\n";
		snapshot_dir(p, 0, std::cout);
		std::cout << "\n[press any key]" << std::flush;
		std::cin.get();
		return "snapshot done";
	}
	return "unknown command: " + cmd + " (try 'help')";
}

// --- Open file in xdg-open ---------------------------------------------------

static void open_with_default(const std::string &path)
{
	pid_t pid = fork();
	if (pid == 0)
	{
		// child: detach from terminal
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0)
		{
			dup2(fd, 1);
			dup2(fd, 2);
			close(fd);
		}
		execlp("xdg-open", "xdg-open", path.c_str(), (char *)nullptr);
		_exit(127);
	}
	// don't wait — let viewer live independently
}

// --- Main loop ----------------------------------------------------------------

static int read_key()
{
	char c;
	if (read(STDIN_FILENO, &c, 1) != 1)
		return -1;
	if (c == '\x1b')
	{
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1)
			return '\x1b';
		if (seq[0] == '[')
		{
			switch (seq[1])
			{
			case 'A':
				return 1000; // up
			case 'B':
				return 1001; // down
			case 'C':
				return 1002; // right
			case 'D':
				return 1003; // left
			}
		}
		return '\x1b';
	}
	return (unsigned char)c;
}

int main()
{
	install_signal_handlers();
	std::string cwd = get_cwd();
	std::stack<std::string> back, fwd;

	auto entries = list_dir(cwd);
	int cursor = 0, scroll = 0;
	std::string status;

	enable_raw_mode();

	auto refresh = [&](const std::string &msg = "")
	{
		WinSize ws = term_size();
		int avail = ws.rows - 4;
		if (avail < 1)
			avail = 1;
		if (cursor < 0)
			cursor = 0;
		if (cursor >= (int)entries.size())
			cursor = std::max(0, (int)entries.size() - 1);
		if (cursor < scroll)
			scroll = cursor;
		if (cursor >= scroll + avail)
			scroll = cursor - avail + 1;
		render(cwd, entries, cursor, scroll, msg);
	};
	refresh();

	while (true)
	{
		int k = read_key();
		if (k < 0)
			break;

		switch (k)
		{
		case 1000: // up
			if (cursor > 0)
				--cursor;
			refresh();
			break;
		case 1001: // down
			if (cursor + 1 < (int)entries.size())
				++cursor;
			refresh();
			break;
		case 1003: // left = back
			if (!back.empty())
			{
				fwd.push(cwd);
				cwd = back.top();
				back.pop();
				if (chdir(cwd.c_str()) != 0)
				{
				}
				entries = list_dir(cwd);
				cursor = 0;
				scroll = 0;
			}
			refresh();
			break;
		case 1002: // right = forward
			if (!fwd.empty())
			{
				back.push(cwd);
				cwd = fwd.top();
				fwd.pop();
				if (chdir(cwd.c_str()) != 0)
				{
				}
				entries = list_dir(cwd);
				cursor = 0;
				scroll = 0;
			}
			refresh();
			break;
		case 127:
		{ // backspace -> parent
			std::string parent = cwd;
			auto pos = parent.find_last_of('/');
			if (pos == 0)
				parent = "/";
			else if (pos != std::string::npos)
				parent = parent.substr(0, pos);
			if (parent != cwd && is_dir(parent))
			{
				back.push(cwd);
				cwd = parent;
				if (chdir(cwd.c_str()) != 0)
				{
				}
				entries = list_dir(cwd);
				cursor = 0;
				scroll = 0;
				while (!fwd.empty())
					fwd.pop();
			}
			refresh();
			break;
		}
		case 'h':
		{
			back.push(cwd);
			cwd = home_dir();
			if (chdir(cwd.c_str()) != 0)
			{
			}
			entries = list_dir(cwd);
			cursor = 0;
			scroll = 0;
			while (!fwd.empty())
				fwd.pop();
			refresh();
			break;
		}
		case '\n':
		case '\r':
		{
			if (entries.empty())
			{
				refresh();
				break;
			}
			const Entry &e = entries[cursor];
			std::string full = cwd + "/" + e.name;
			if (e.name == "..")
			{
				auto pos = cwd.find_last_of('/');
				full = (pos == 0) ? "/" : cwd.substr(0, pos);
			}
			if (is_dir(full))
			{
				back.push(cwd);
				while (!fwd.empty())
					fwd.pop();
				cwd = full;
				if (chdir(cwd.c_str()) != 0)
				{
				}
				entries = list_dir(cwd);
				cursor = 0;
				scroll = 0;
				refresh();
			}
			else
			{
				open_with_default(full);
				refresh("opened " + e.name);
			}
			break;
		}
		case ':':
		{
			disable_raw_mode();
			WinSize ws = term_size();
			move_cursor(ws.rows, 1);
			clear_line();
			std::cout << ANSI_GRN << ":" << ANSI_RESET << std::flush;
			std::string line;
			std::getline(std::cin, line);
			bool quit = false;
			std::string new_cwd;
			std::string msg = run_command(tokenize(line), cwd, new_cwd, quit);
			if (quit)
			{
				clear_screen();
				return 0;
			}
			if (!new_cwd.empty())
			{
				back.push(cwd);
				while (!fwd.empty())
					fwd.pop();
				cwd = new_cwd;
				if (chdir(cwd.c_str()) != 0)
				{
				}
			}
			entries = list_dir(cwd);
			if (cursor >= (int)entries.size())
				cursor = 0;
			enable_raw_mode();
			refresh(msg);
			break;
		}
		case 'q':
			clear_screen();
			return 0;
		default:
			refresh();
			break;
		}
	}
	return 0;
}
