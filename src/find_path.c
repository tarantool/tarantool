#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#if defined(__APPLE__)
	#include <mach-o/dyld.h>
#elif defined(__FreeBSD__)
  #include <sys/sysctl.h>
#endif

const char *
find_path(const char *argv0)
{
	static char path[PATH_MAX];
	static bool found = false;

	if (found)
		return path;

	char buf[PATH_MAX];
	uint32_t size = PATH_MAX - 1;
	if (argv0[0] == '/')
		snprintf(buf, size, "%s", argv0);
	else {
		int rc;
#if defined(__linux__)
		rc = readlink("/proc/self/exe", buf, size);
#elif defined(__FreeBSD__)
		int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
		rc = sysctl(mib, 4, buf, &size, NULL, 0);
#elif defined(__sun)
		snprintf(buf, size, "%s", getexecname());
		rc = 0;
#elif defined(__APPLE__)
		rc = _NSGetExecutablePath(buf, &size);
#else
		rc = -1;
#endif
		if (rc == -1)
			snprintf(buf, sizeof(buf) - 1, "%s", getenv("_"));
	}
	realpath(buf, path);
	found = true;
	return path;
}
