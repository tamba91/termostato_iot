#ifndef _GNU_SOURCE     //utilizzato durante il testing su linux non necessario su esp
#define _GNU_SOURCE
#endif

#include <limits.h>
#include "timeinterval.h"
#include <stdio.h>

/*inserisce un intervallo temporale in un array di intervalli, verificando e risolvendo eventuali sovrapposizioni
  ritorna true se l'intervallo viene inserito con successo nell'array, false altrimenti
*/

bool insert_into_interval_array(daytime_interval_sec_t arr[], const char *start_time, const char *end_time, const int size)
{
    struct tm temp = {0};
    daytime_interval_sec_t interval_to_insert = {0};

    strptime(start_time, "%H:%M:%S", &temp);
    interval_to_insert.start_sec = temp.tm_hour * SECONDS_PER_HOUR + temp.tm_min * SECONDS_PER_MINUTE + temp.tm_sec;

    temp.tm_hour = 0;
    temp.tm_min = 0;
    temp.tm_sec = 0;

    strptime(end_time, "%H:%M:%S", &temp);
    interval_to_insert.end_sec = temp.tm_hour * SECONDS_PER_HOUR + temp.tm_min * SECONDS_PER_MINUTE + temp.tm_sec;
    if(interval_to_insert.end_sec == 0)
        interval_to_insert.end_sec = SECONDS_PER_DAY;

    if (interval_to_insert.start_sec >= 0 && interval_to_insert.start_sec < SECONDS_PER_DAY && interval_to_insert.end_sec > 0 && interval_to_insert.end_sec <= SECONDS_PER_DAY && interval_to_insert.start_sec < interval_to_insert.end_sec)
    {
        int index = 0;
        while (index < size && arr[index].end_sec < interval_to_insert.start_sec)
            ++index;

        if(index == size)
            return false;

        if (IS_FREE_BOX(arr[index]))
        {
            arr[index].start_sec = interval_to_insert.start_sec;
            arr[index].end_sec = interval_to_insert.end_sec;
            return true;
        }

        else if (!(IS_FREE_BOX(arr[index])) && interval_to_insert.start_sec >= arr[index].start_sec && interval_to_insert.end_sec <= arr[index].end_sec)
            return true;

        else if (!(IS_FREE_BOX(arr[index])) && (interval_to_insert.end_sec < arr[index].start_sec) && (IS_FREE_BOX(arr[size - 1])))
        {
            for (int i = size - 2; i >= index; i--)
            {
                arr[i + 1].start_sec = arr[i].start_sec;
                arr[i + 1].end_sec = arr[i].end_sec;
            }

            arr[index].start_sec = interval_to_insert.start_sec;
            arr[index].end_sec = interval_to_insert.end_sec;
            return true;
        }

        else if (!(IS_FREE_BOX(arr[index])) && interval_to_insert.end_sec >= arr[index].start_sec)
        {
            if (interval_to_insert.start_sec < arr[index].start_sec)
                arr[index].start_sec = interval_to_insert.start_sec;

            int curr = index;

            while (curr < size - 1 && arr[curr + 1].start_sec <= interval_to_insert.end_sec)
                curr++;

            if (interval_to_insert.end_sec > arr[curr].end_sec)
                arr[index].end_sec = interval_to_insert.end_sec;
            else
                arr[index].end_sec = arr[curr].end_sec;

            for (int i = curr + 1; i < size; i++)
                arr[++index] = arr[i];

            for (int i = index + 1; i < size; i++)
            {
                arr[i].start_sec = INT_MAX;
                arr[i].end_sec = INT_MAX;
            }
            return true;
        }

        else
            return false;
    }

    else
        return false;
}

/*inizializza un array di intervalli temporali*/

void init_interval_array(daytime_interval_sec_t arr[], int size)
{
    for(int i = 0; i<size; i++)
    {
        arr[i].start_sec = INT_MAX;
        arr[i].end_sec = INT_MAX;
    }
}

/*stampa su stringa un array di intervalli temporali, ritorna il numero di caratteri scritti*/

int sprint_intervals(const daytime_interval_sec_t arr[], const int arrsize, char *dest, const int destsize)
{
    struct tm tmp = {0};
    int index = 0;
    int offset = 0;

    if(destsize < 13)
        return 0;

    while(index < arrsize && destsize - offset >= 13 && !IS_FREE_BOX(arr[index]))
    {
        tmp.tm_hour = arr[index].start_sec / SECONDS_PER_HOUR;
        tmp.tm_min = (arr[index].start_sec % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
        offset += strftime(dest + offset, destsize, "%H:%M/", &tmp);
        tmp.tm_hour = arr[index].end_sec / SECONDS_PER_HOUR;
        tmp.tm_min = (arr[index].end_sec % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
        offset += strftime(dest + offset, destsize, "%H:%M, ", &tmp);
        ++index;
    }
    if(index > 0)
    {
        offset -= 2;
        dest[offset] = '\0';
        return offset;
    }

    else
    {
        dest[0] = '\0';
        return 0;
    }
            
}

/*verifica se un orario dato è compreso in un intervallo dell'array di intervalli*/

bool time_in_interval(const struct tm *test_time, const daytime_interval_sec_t arr[], const int arrsize)
{
    int test_time_sec = test_time->tm_hour * SECONDS_PER_HOUR + test_time->tm_min * SECONDS_PER_MINUTE + test_time->tm_sec;

    if(test_time_sec < 0 || test_time_sec > SECONDS_PER_DAY)
        return false;
    
    int index = 0;

    while (index < arrsize && arr[index].end_sec < test_time_sec)
        ++index;
    
    if(index == arrsize)
        return false;
    
    if(test_time_sec >= arr[index].start_sec && test_time_sec <= arr[index].end_sec)
        return true;
    else
        return false;
}