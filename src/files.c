#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONF_SIZE_MAX (1 << 20)

int draw_user_dir(char *buf, size_t n) {
	const char *home = getenv("HOME");
	int r;

	if (!home || home[0] != '/')
		return -1;
	r = snprintf(buf, n, "%s/.config/tundra", home);
	return r < 0 || (size_t)r >= n ? -1 : 0;
}

int draw_read_conf(const char *dir, const char *name, char **text, size_t *len,
                   char err[DRAW_ERR_MAX]) {
	char path[4096];
	struct stat st;
	char *buf;
	size_t got = 0;
	int fd, r = snprintf(path, sizeof(path), "%s/%s", dir, name);

	*text = NULL;
	*len = 0;
	if (r < 0 || (size_t)r >= sizeof(path)) {
		snprintf(err, DRAW_ERR_MAX, "%.150s/%.40s: path too long", dir, name);
		return -1;
	}
	if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0) {
		if (errno == ENOENT || errno == ENOTDIR)
			return 0;
		snprintf(err, DRAW_ERR_MAX, "%.200s: %.100s", path, strerror(errno));
		return -1;
	}
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > CONF_SIZE_MAX) {
		snprintf(err, DRAW_ERR_MAX, "%.200s: not a file of up to 1 MiB", path);
		close(fd);
		return -1;
	}
	if (!(buf = malloc((size_t)st.st_size + 1))) {
		snprintf(err, DRAW_ERR_MAX, "out of memory");
		close(fd);
		return -1;
	}
	while (got < (size_t)st.st_size) {
		ssize_t n = read(fd, buf + got, (size_t)st.st_size - got);

		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			snprintf(err, DRAW_ERR_MAX, "%.200s: %.100s", path,
			         strerror(errno));
			free(buf);
			close(fd);
			return -1;
		}
		if (!n)
			break;
		got += (size_t)n;
	}
	close(fd);
	buf[got] = 0;
	*text = buf;
	*len = got;
	return 0;
}
