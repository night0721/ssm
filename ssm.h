#ifndef SSM_H
#define SSM_H

#define VERSION "1.0.0"
#define DATABASE_PATH "~/.local/share/ssm.tsv"

// Code editor
static const char editor[] = "nvim";
// Notification notifier
static const char notifier[] = "luft";

typedef enum {
    LOW,
    MEDIUM,
    HIGH,
} priority_type;

typedef enum {
    NONE,
    DAILY,
    WEEKLY,
    MONTHLY,
    YEARLY,
} recurrence_type;

typedef enum {
	FULL,
	HHMM,
} datefmt;

typedef struct {
    time_t timestamp;
    int is_valid;
} parsed_time;

typedef struct {
    time_t timestamp;
    char name[50];
    char description[100];
	priority_type priority;
    recurrence_type recurrence;
    int recurrence_interval; /*Number of days/weeks/months/years between occurrences */
    time_t recurrence_end; /* End date for recurrence */
    int notified;
} event;

#endif
