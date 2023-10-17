#ifndef _TIMEINTERVAL_H
#define _TIMEINTERVAL_H

#include <stdbool.h>
#include <time.h>

#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define SECONDS_PER_MINUTE 60

#define IS_FREE_BOX(daytime_interval_sec_t) ((daytime_interval_sec_t.start_sec == INT_MAX) || (daytime_interval_sec_t.end_sec == INT_MAX))

typedef struct
{
    int start_sec;
    int end_sec;
} daytime_interval_sec_t;

bool insert_into_interval_array(daytime_interval_sec_t arr[], const char *start_time, const char *end_time, const int size);
void init_interval_array(daytime_interval_sec_t arr[], int size);
int sprint_intervals(const daytime_interval_sec_t arr[], const int arrsize, char *dest, const int destsize);
bool time_in_interval(const struct tm *test_time, const daytime_interval_sec_t arr[], const int arrsize);

#endif