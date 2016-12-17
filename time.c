#include <sys/time.h>
#include <stdio.h>

#include "time.h"

void
print_time(struct timeval* tv)
{
	char tmbuf[64];
	strftime(tmbuf, sizeof tmbuf, "%H:%M:%S", localtime(&tv->tv_sec));
	printf("%s.%03d\n", tmbuf, (tv->tv_usec / 1000));
}

long
ms_elapsed(struct timeval* start, struct timeval* end)
{
	return 1000 * (end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec) / 1000;
}

