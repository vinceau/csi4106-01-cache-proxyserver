#ifndef TIME_H
#define TIME_H

void
print_time(struct timeval* tv);

long
ms_elapsed(struct timeval* start, struct timeval* end);

#endif
