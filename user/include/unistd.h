#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int open(const char *path, int flags, ...);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);
int unlink(const char *path);
int rmdir(const char *path);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int access(const char *path, int mode);
int isatty(int fd);
int dup(int fd);
int dup2(int oldfd, int newfd);
pid_t getpid(void);
pid_t fork(void);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
unsigned int sleep(unsigned int sec);
int usleep(unsigned int usec);
int fsync(int fd);
int truncate(const char *path, off_t len);
int ftruncate(int fd, off_t len);
char *getlogin(void);
#endif
