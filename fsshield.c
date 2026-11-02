/* See LICENSE file for copyright and license details. */
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sched.h>
#include <libsimple.h>
#include <libsimple-arg.h>

NUSAGE(125, "[-a file] ... [-r file] ... [-t file] ... [-nR] utility [argument] ...");


enum type {
	ACCESSIBLE,
	READONLY,
	TMPFS
};

struct file {
	const char *path;
	enum type type;
};


static struct file *files = NULL;
static size_t nfiles = 0u;

static const struct file preset[] = {
	{".",           ACCESSIBLE},
	{"/tmp",        TMPFS},
	{"/var/tmp",    TMPFS},
	{"/var/empty",  READONLY},
	{"/etc/passwd", READONLY},
	{"/usr",        READONLY},
	{"/bin",        READONLY},
	{"/sbin",       READONLY},
	{"/lib",        READONLY},
	{"/lib64",      READONLY},
	{"/lib32",      READONLY},
	{"/libexec",    READONLY}
};

static size_t npreset = ELEMSOF(preset);

static char *cwd;


static void
add(const char *path, enum type type)
{
	if (!*path)
		usage();
	files[nfiles].path = path;
	files[nfiles].type = type;
	nfiles += 1u;
}


static void
make_dirs(char *path, mode_t mode, int make_top)
{
	char *p;
	for (p = &path[1u]; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(path, mode) && errno != EEXIST)
			eprintf("mkdir %s %03o:", path, mode);
		*p = '/';
	}
	if (make_top)
		if (mkdir(path, mode) && errno != EEXIST)
			eprintf("mkdir %s %03o:", path, mode);
}


static void
touch(const char *path, mode_t mode)
{
	int fd;
	if (!faccessat(AT_FDCWD, path, F_OK, AT_SYMLINK_NOFOLLOW))
		return;
	fd = open(path, O_CREAT | O_WRONLY, mode);
	if (fd < 0)
		eprintf("open %s O_CREAT|O_WRONLY %03o:", path, mode);
	close(fd);
}


static char *
clean_path(const char *path)
{
	char *ret, *r, *w;

	if (*path != '/') {
		ret = emalloc(strlen(cwd) + strlen(path) + sizeof("/"));
		stpcpy(stpcpy(stpcpy(ret, cwd), "/"), path);
	} else {
		ret = estrdup(path);
	}

	r = w = &ret[1u];

	while (*r) {
		while (*r == '/') r++;
		if (r[0u] == '.' && r[1u] == '/') {
			r = &r[2u];
		} else if (r[0u] == '.' && !r[1u]) {
			break;
		} else if (r[0u] == '.' && r[1u] == '.' && (r[2u] == '/' || !r[2u])) {
			if (--w != ret)
				while (*w != '/')
					w--;
			w++;
			r = &r[2u];
			if (!*r++)
				break;
		} else {
			do {
				*w++ = *r;
			} while (*r && *r++ != '/');
		}
	}

	if (w[-1] == '/' && w != &ret[1u])
		w--;

	*w = '\0';

	if (!strcmp(ret, "/.oldroot"))
		eprintf("/.oldroot must not be included as an accessible path");

	return ret;
}


static int
cmpfiles(const void *av, const void *bv)
{
	const struct file *a = av;
	const struct file *b = bv;
	int cmp = strcmp(a->path, b->path);
	return cmp ? cmp : (int)a->type - (int)b->type;
}


static int
isdir(const char *path)
{
	struct stat st;
	return stat(path, &st) || S_ISDIR(st.st_mode);
}


static void
setup_filesystem(void)
{
	struct mount_attr attr, ro = {.attr_set = MOUNT_ATTR_RDONLY};
	char root[] = "/tmp/fsshield.XXXXXX";
	char *pathbuf;
	size_t i, len, maxlen = sizeof("/.oldroot") - 1u;
	int fds[2];
	pid_t pid;

	if (socketpair(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fds))
		eprintf("socketpair PF_LOCAL SOCK_STREAM|SOCK_CLOEXEC 0:");

	if (!mkdtemp(root))
		eprintf("mkdtemp:");

	pid = fork();
	switch (pid) {
	case -1:
		eprintf("fork:");
	case 0:
		close(fds[1]);
		pid = fork();
		switch (pid) {
		case -1:
			eprintf("fork:");
		case 0:
			if (write(fds[0], &(char){0}, 1u) != 1)
				eprintf("write <socket>:");
			if (read(fds[0], &(char){0}, 1u) != 0)
				eprintf("read <socket>:");
			close(fds[0]);
			if (rmdir(root))
				weprintf("rmdir %s:", root);
			break;
		default:
			break;
		}
		exit(0);
	default:
		close(fds[0]);
		switch (read(fds[1], &(char){0}, 1u)) {
		case 0:
			exit(125);
		case 1:
			break;
		default:
			eprintf("read <socket>:");
		}
		waitpid(pid, NULL, 0);
		break;
	}

	for (i = 0u; i < nfiles; i++) {
		len = strlen(files[i].path);
		maxlen = MAX(maxlen, len);
	}
	pathbuf = emalloc(maxlen + sizeof(root));

	if (unshare(CLONE_NEWNS))
		eprintf("unshare CLONE_NEWNS:");

	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
		eprintf("mount NULL / NULL MS_REC|MS_PRIVATE NULL:");

	if (mount("tmpfs", root, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755"))
		eprintf("mount tmpfs %s tmpfs MS_NOSUID|MS_NODEV mode=0755:", root);

	for (i = 0u; i < nfiles; i++) {
		stpcpy(stpcpy(pathbuf, root), files[i].path);
		switch (files[i].type) {
		case ACCESSIBLE:
		case READONLY:
			if (isdir(files[i].path)) {
				make_dirs(pathbuf, 0755, 1);
				if (mount(files[i].path, pathbuf, NULL, MS_BIND | MS_REC, NULL))
					eprintf("mount %s %s NULL MS_BIND|MS_REC NULL:", files[i].path, pathbuf);
			} else {
				make_dirs(pathbuf, 0755, 0);
				touch(pathbuf, 0644);
				if (mount(files[i].path, pathbuf, NULL, MS_BIND, NULL))
					eprintf("mount %s %s NULL MS_BIND NULL:", files[i].path, pathbuf);
			}
			if (files[i].type == READONLY) {
				attr = ro;
				if (mount_setattr(AT_FDCWD, pathbuf, AT_RECURSIVE, &attr, sizeof(attr)))
					eprintf("mount_setattr AT_FDCWD %s AT_RECURSIVE {.attr_set = MOUNT_ATTR_RDONLY}:", pathbuf);
			}
			break;
		case TMPFS:
			make_dirs(pathbuf, 01777, 1);
			if (mount("tmpfs", pathbuf, "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777"))
				eprintf("mount tmpfs %s tmpfs MS_NOSUID|MS_NODEV mode=1777:", pathbuf);
			break;
		default:
			abort();
		}
	}

	stpcpy(stpcpy(pathbuf, root), "/.oldroot");
	make_dirs(pathbuf, 0700, 1);
	if (chdir(root))
		eprintf("chdir %s:", root);
	if (syscall(SYS_pivot_root, ".", ".oldroot"))
		eprintf("pivot_root . .oldroot (from %s):", root);
	if (chdir(cwd))
		eprintf("chdir %s:", cwd);
	if (umount2("/.oldroot", MNT_DETACH))
		eprintf("umount2 /.oldroot MNT_DETACH");
	if (rmdir("/.oldroot"))
		eprintf("rmdir /.oldroot");
	free(pathbuf);
}


int
main(int argc, char *argv[])
{
	size_t i, n;
	enum type cwd_access = ACCESSIBLE;

	libsimple_default_failure_exit = 125;

	argv0 = argv[0];
	files = ecalloc((size_t)argc + npreset, sizeof(*files));

	ARGBEGIN {
	case 'a': add(ARG(), ACCESSIBLE); break;
	case 'r': add(ARG(), READONLY);   break;
	case 't': add(ARG(), TMPFS);      break;
	case 'n': npreset = 1u;           break;
	case 'R': cwd_access = READONLY;  break;
	default:
		usage();
	} ARGEND;

	if (!argc)
		usage();

	files[nfiles] = preset[0u];
	files[nfiles++].type = cwd_access;
	for (i = 1u; i < npreset; i++)
		if (!faccessat(AT_FDCWD, preset[i].path, F_OK, AT_SYMLINK_NOFOLLOW))
			files[nfiles++] = preset[i];

	cwd = get_current_dir_name();
	if (!cwd)
		eprintf("get_current_dir_name:");
	if (*cwd != '/')
		abort();

	for (i = 0u; i < nfiles; i++)
		files[i].path = clean_path(files[i].path);

	qsort(files, nfiles, sizeof(*files), &cmpfiles);
	for (i = n = 1u; i < nfiles; i++)
		if (strcmp(files[i].path, files[n - 1u].path))
			files[n++] = files[i];
	nfiles = n;

	setup_filesystem();
	unsetenv("PWD");

	if (setegid(getgid()))
		eprintf("setegid <real group>:");
	if (seteuid(getuid()))
		eprintf("seteuid <real user>:");

	execvp(argv[0], argv);
	enprintf(errno == ENOENT ? 127 : 126, "execvp %s:", argv[0]);
}
