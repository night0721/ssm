#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>

#include "ssm.h"

#define BUF_LEN (10 * (sizeof(struct inotify_event) + PATH_MAX + 1))
#define MAX_LINE_LEN 4096
#define SECONDS_PER_DAY 86400
#define NOTIFICATION_THRESHOLD 300 /* 5 minutes */

char *get_database_path(void)
{
	char *db = DATABASE_PATH;
	char *db_path;
	if (db[0] == '~') {
		char *home = getenv("HOME");
		if (home == NULL) {
			fprintf(stderr, "$HOME not defined\n");
			return NULL;
		}
		db_path = malloc((strlen(db) + strlen(home)) * sizeof(char));
		if (db_path == NULL) {
			perror("malloc");
			exit(EXIT_FAILURE);
		}

		/* replace ~ with home */
		snprintf(db_path, strlen(db) + strlen(home), "%s%s", home, db + 1);
	} else {
		db_path = strdup(db);
	}
	return db_path;
}


/*
 * Convert time_t into heap-allocated string
 */
char *convert_timestamp(time_t timestamp, datefmt format)
{
	struct tm *time_info = localtime(&timestamp);
	char *time_buf = malloc(20 * sizeof(char));
	if (time_buf == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	switch(format) {
		case FULL:
			strftime(time_buf, 20, "%Y-%m-%d %H:%M:%S", time_info);
			break;
		case HHMM:
			strftime(time_buf, 6, "%H:%M", time_info);
			break;
		default:
			fprintf(stderr, "Invalid datefmt\n");
	}
	return time_buf;
}

void load_events(event **events, int *num_events)
{
	char *db_path = get_database_path();
	if (db_path == NULL)
		return;
	FILE *file = fopen(db_path, "r");
	if (!file) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}

	char line[MAX_LINE_LEN];
	*num_events = 0;
	while (fgets(line, sizeof(line), file)) {
		(*num_events)++;
	}

	*events = malloc(sizeof(event) * (*num_events));
	if (*events == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	fseek(file, 0, SEEK_SET);
	for (int i = 0; fgets(line, sizeof(line), file); i++) {
		sscanf(line, "%ld\t%49[^\t]\t%99[^\t]\t%d\t%d\t%d\t%ld\n",
				&(*events)[i].timestamp, (*events)[i].name, (*events)[i].description,
				&(*events)[i].priority, &(*events)[i].recurrence, &(*events)[i].recurrence_interval,
				&(*events)[i].recurrence_end);
		(*events)[i].notified = 0;
	}

	fclose(file);
	free(db_path);
}

void add_event(time_t timestamp, const char *name,
              const char *description, priority_type priority,
              recurrence_type recurrence, int recurrence_interval,
              time_t recurrence_end)
{
	char *db_path = get_database_path();
	FILE *file;
	file = fopen(db_path, access(db_path, F_OK) ? "w" : "a");
	if (!file) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}

	fprintf(file, "%ld\t%s\t%s\t%d\t%d\t%d\t%ld\n", timestamp, name, description, priority, recurrence, recurrence_interval, recurrence_end);
	fclose(file);
	free(db_path);
	char *time_buf = convert_timestamp(timestamp, 1);
	if (recurrence != NONE) {
		char *end_buf = convert_timestamp(recurrence_end, 1);
		printf("Added \"%s\" with description \"%s\" at \"%s\" (recurs every %d %s until %s)\n", name, description, time_buf, recurrence_interval,
				recurrence == DAILY ? "day" :
				recurrence == WEEKLY ? "week" :
				recurrence == MONTHLY ? "month" : "year", end_buf);
		free(end_buf);
		return;
	} else {
		printf("Added \"%s\" with description \"%s\" at \"%s\"\n", name, description, time_buf);
	}
	free(time_buf);
}

static void send_notification(event to_alert)
{
	char *time_buf = convert_timestamp(to_alert.timestamp, 1);
	char description_buf[512];
	snprintf(description_buf, sizeof(description_buf),
			"Event \"%s\" starts at %s - %s", to_alert.name, time_buf,
			to_alert.description);

	free(time_buf);

	int pid = fork();
	if (pid == 0) {
		/* Child */
		execlp(notifier, notifier, "Upcoming event", description_buf, NULL);
		_exit(1);
	} else if (pid > 0) {
		/* Parent */
	} else {
		perror("fork");
	}
}

void check_events(event *events, int *num_events)
{
	time_t now = time(NULL);
	for (int i = 0; i < *num_events; i++) {
		if (!events[i].notified &&
				events[i].timestamp > now &&
				events[i].timestamp - now <= NOTIFICATION_THRESHOLD) {

			send_notification(events[i]);
			events[i].notified = 1;
		}
	}
}

static char *get_relative_day(time_t event_time)
{
	time_t now = time(NULL);
	int days = (event_time - now) / SECONDS_PER_DAY;
	static char buffer[32];

	if (days == 0) return "today";
	else if (days == 1) return "tomorrow";
	else sprintf(buffer, "in %d days", days);
	return buffer;
}

int is_today(time_t t)
{
	time_t now = time(NULL);
	struct tm *time_tm = localtime(&t);
	struct tm *now_tm = localtime(&now);
	return (time_tm->tm_year == now_tm->tm_year &&
			time_tm->tm_mon == now_tm->tm_mon &&
			time_tm->tm_mday == now_tm->tm_mday);
}

void print_event(event e, int relative)
{
	char *time_str = convert_timestamp(e.timestamp, relative ? FULL : HHMM);
	if (relative) {
		char *relative_day = get_relative_day(e.timestamp);
		printf("%s (%s): %s - %s\n", time_str, relative_day, e.name, e.description);
	} else {
		printf("%s: %s - %s\n", time_str, e.name, e.description);
	}
	free(time_str);
}

void list_today_events(event *events, int num_events)
{
	printf("\nToday's events:\n");
	printf("---------------\n");
	int found = 0;

	for (int i = 0; i < num_events; i++) {
		if (is_today(events[i].timestamp)) {
			print_event(events[i], 0);
			found = 1;
		}
	}

	if (!found) {
		printf("No events scheduled for today\n");
	}
}

void list_upcoming_events(event *events, int num_events)
{
	printf("\nUpcoming events (next 28 days):\n");
	printf("------------------------------\n");
	time_t now = time(NULL) + SECONDS_PER_DAY;
	time_t week_later = now + (28 * SECONDS_PER_DAY);
	int found = 0;

	for (int i = 0; i < num_events; i++) {
		if (events[i].timestamp > now && events[i].timestamp <= week_later) {
			print_event(events[i], 1);
			found = 1;
		}
	}

	if (!found) {
		printf("No upcoming events in the next 7 days\n");
	}
}

void expand_recurring_events(event **events, int *num_events)
{
	int new_count = *num_events;
	time_t now = time(NULL);

	/* Count how many new events we'll need */
	for (int i = 0; i < *num_events; i++) {
		if ((*events)[i].recurrence != NONE) {
			time_t next_occurrence = (*events)[i].timestamp;
			while (next_occurrence <= (*events)[i].recurrence_end) {
				if (next_occurrence >= now) {
					new_count++;
				}

				/* Calculate next occurrence based on recurrence type */
				switch ((*events)[i].recurrence) {
					case DAILY:
						next_occurrence += SECONDS_PER_DAY * (*events)[i].recurrence_interval;
						break;
					case WEEKLY:
						next_occurrence += SECONDS_PER_DAY * 7 * (*events)[i].recurrence_interval;
						break;
					case MONTHLY:
						next_occurrence += SECONDS_PER_DAY * 30 * (*events)[i].recurrence_interval;
						break;
					case YEARLY:
						next_occurrence += SECONDS_PER_DAY * 365 * (*events)[i].recurrence_interval;
						break;
					default:
						break;
				}
			}
		}
	}

	event *new_events = malloc(sizeof(event) * new_count);
	if (!new_events) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	/* Copy original events and expand recurring ones */
	int current_index = 0;
	for (int i = 0; i < *num_events; i++) {
		if ((*events)[i].recurrence != NONE) {
			time_t next_occurrence = (*events)[i].timestamp;
			while (next_occurrence <= (*events)[i].recurrence_end) {
				if (next_occurrence >= now) {
					event new_event = (*events)[i];
					new_event.timestamp = next_occurrence;
					memcpy(&new_events[current_index++], &new_event, sizeof(event));
				}

				switch ((*events)[i].recurrence) {
					case DAILY:
						next_occurrence += SECONDS_PER_DAY * (*events)[i].recurrence_interval;
						break;
					case WEEKLY:
						next_occurrence += SECONDS_PER_DAY * 7 * (*events)[i].recurrence_interval;
						break;
					case MONTHLY:
						next_occurrence += SECONDS_PER_DAY * 30 * (*events)[i].recurrence_interval;
						break;
					case YEARLY:
						next_occurrence += SECONDS_PER_DAY * 365 * (*events)[i].recurrence_interval;
						break;
					default:
						break;
				}
			}
		} else {
			memcpy(&new_events[current_index++], &(*events)[i], sizeof(event));
		}
	}

	*events = new_events;
	*num_events = new_count;
}

static int compare_by_start_time(const void *a, const void *b)
{
	return ((event *) a)->timestamp - ((event *) b)->timestamp;
}

static int compare_by_priority(const void *a, const void *b)
{
	return ((event *) b)->priority - ((event *) a)->priority;
}

void sort_events(event *events, int num_events, int sort_type)
{
	switch (sort_type) {
		/* Start time */
		case 0:
			qsort(events, num_events, sizeof(event), compare_by_start_time);
			break;
			/* priority_type (NOT IMPLEMENTED) */
		case 1:
			qsort(events, num_events, sizeof(event), compare_by_priority);
			break;
	}
}

void watch_file(event *events, int *num_events)
{
	load_events(&events, num_events);
	expand_recurring_events(&events, num_events);
	while (1) {
		// daemon to show notifications
		check_events(events, num_events);
	}
	
	free(events);
}

parsed_time parse_relative_time(const char *time_str)
{
	parsed_time result = {0, 0};
	char *str = strdup(time_str);
	char *number = str;
	char *unit = str;

	/* Find the separation between number and unit */
	while (*unit && isdigit(*unit)) unit++;
	if (*unit) {
		*unit = '\0';
		unit++;
	}

	if (!*number || !*unit) {
		free(str);
		return result;
	}

	int value = atoi(number);
	time_t now = time(NULL);

	if (strcmp(unit, "min") == 0 || strcmp(unit, "mins") == 0) {
		result.timestamp = now + (value * 60);
	} else if (strcmp(unit, "hour") == 0 || strcmp(unit, "hours") == 0) {
		result.timestamp = now + (value * 3600);
	} else if (strcmp(unit, "day") == 0 || strcmp(unit, "days") == 0) {
		result.timestamp = now + (value * 86400);
	} else if (strcmp(unit, "week") == 0 || strcmp(unit, "weeks") == 0) {
		result.timestamp = now + (value * 86400 * 7);
	} else {
		free(str);
		return result;
	}

	result.is_valid = 1;
	free(str);
	return result;
}

parsed_time parse_absolute_time(const char *time_str)
{
	parsed_time result = {0, 0};
	struct tm tm = {0};
	char *formats[] = {
		"%Y-%m-%d %H:%M:%S",
		"%Y-%m-%d %H:%M",
		"%Y/%m/%d %H:%M:%S",
		"%Y/%m/%d %H:%M",
		"%d-%m-%Y %H:%M:%S",
		"%d-%m-%Y %H:%M",
		"%d/%m/%Y %H:%M:%S",
		"%d/%m/%Y %H:%M",
		"%d-%m-%Y",
		"%Y-%m-%d",
		"%d/%m/%Y",
		"%Y/%m/%d",
		"%H:%M",
	};

	for (int i = 0; i < sizeof(formats)/sizeof(formats[0]); i++) {
		char *parsed = strptime(time_str, formats[i], &tm);
		if (parsed != NULL) {
			/* Use today's date */
			if (strcmp(formats[i], "%H:%M") == 0) {
				time_t now = time(NULL);
				struct tm *today = localtime(&now);
				tm.tm_year = today->tm_year;
				tm.tm_mon = today->tm_mon;
				tm.tm_mday = today->tm_mday;
			}

			tm.tm_isdst = -1;
			result.timestamp = mktime(&tm);
			result.is_valid = 1;
			return result;
		}
	}

	return result;
}

parsed_time parse_time_string(const char *time_str)
{
	/* Parsing as relative time first */
	parsed_time result = parse_relative_time(time_str);
	if (result.is_valid) return result;

	/* Try absolute time */
	return parse_absolute_time(time_str);
}

static void usage(int code)
{
	fprintf(code ? stderr : stdout,
			"Simple Scheduler Manager " VERSION "\n\n"
			"Usage: ssm <command>\n\n"
			"	help						Show this help message\n"
			"	sched <time> <title> <description> [options]	Schedule an event\n"
			"	edit						Edit schedule with $EDITOR\n"
			"	list						List all upcoming events\n"
			"	run						Spawn notifier daemon\n"
		   );
	exit(code);
}

int main(int argc, char **argv)
{
	event *events = NULL;
	int num_events = 0;
	if (argc == 1 || (argc == 2 && !strncmp(argv[1], "list", 4))) {
		/* Load and expand recurring events */
		char *time_buf = convert_timestamp(time(NULL), 0);
		printf("Current date: %s\n", time_buf);
		free(time_buf);

		load_events(&events, &num_events);
		list_today_events(events, num_events);

		expand_recurring_events(&events, &num_events);

		/* Sort events by start time */
		sort_events(events, num_events, 0);
		list_upcoming_events(events, num_events);
		return EXIT_SUCCESS;
	}
	if (strcmp(argv[1], "sched") == 0) {
		if (argc < 4) {
			fprintf(stderr, "Usage: ssm sched <time> <title> <description> [options]\n");
			fprintf(stderr, "Options:\n");
			fprintf(stderr, "  --priority <low|medium|high>\n");
			fprintf(stderr, "  --recur <daily|weekly|monthly|yearly>\n");
			fprintf(stderr, "  --interval <number>\n");
			fprintf(stderr, "  --until <end-date>\n");
			fprintf(stderr, "\nTime formats:\n");
			fprintf(stderr, "  Relative: 30 min, 2 hours, 1 day, 2 weeks\n");
			fprintf(stderr, "  Absolute: YYYY-MM-DD DD-MM-YY [HH:MM:SS] HH:MM[:SS]\n");
			return EXIT_FAILURE;
		}

		char *when = argv[2];
		char *name = argv[3];
		char *description = argv[4];

		parsed_time pt = parse_time_string(when);
		if (!pt.is_valid) {
			fprintf(stderr, "Invalid time format: %s\n", when);
			return EXIT_FAILURE;
		}

		priority_type priority = MEDIUM;
		recurrence_type recurrence = NONE;
		int recurrence_interval = 1;
		time_t recurrence_end = 0;

		/* Parse optional arguments */
		for (int i = 5; i < argc; i++) {
			if (!strcmp(argv[i], "--priority") && i + 1 < argc) {
				if (!strcmp(argv[i + 1], "low")) priority = LOW;
				else if (!strcmp(argv[i + 1], "high")) priority = HIGH;
				i++;
			} else if (!strcmp(argv[i], "--recur") && i + 1 < argc) {
				if (!strcmp(argv[i + 1], "daily")) recurrence = DAILY;
				else if (!strcmp(argv[i + 1], "weekly")) recurrence = WEEKLY;
				else if (!strcmp(argv[i + 1], "monthly")) recurrence = MONTHLY;
				else if (!strcmp(argv[i + 1], "yearly")) recurrence = YEARLY;
				i++;
			} else if (!strcmp(argv[i], "--interval") && i + 1 < argc) {
				recurrence_interval = atoi(argv[i + 1]);
				i++;
			} else if (!strcmp(argv[i], "--until") && i + 1 < argc) {
				parsed_time end_time = parse_time_string(argv[i + 1]);
				if (end_time.is_valid) {
					recurrence_end = end_time.timestamp;
				} else {
					fprintf(stderr, "Invalid end date format: %s\n", argv[i + 1]);
					return EXIT_FAILURE;
				}
				i++;
			}
		}

		add_event(pt.timestamp, name, description, priority, recurrence,
				recurrence_interval, recurrence_end);

		char *time_str = convert_timestamp(pt.timestamp, 0);
		printf("Time: %s\n", time_str);
		printf("Title: %s\n", name);
		printf("Description: %s\n", description);
		printf("Priority: %s\n", priority == LOW ? "Low" : (priority == HIGH ? "High" : "Medium"));
		if (recurrence != NONE) {
			printf("Recurrence: %s (every %d ",
					recurrence == DAILY ? "Daily" :
					recurrence == WEEKLY ? "Weekly" :
					recurrence == MONTHLY ? "Monthly" : "Yearly",
					recurrence_interval);
			printf("%s)\n",
					recurrence == DAILY ? "days" :
					recurrence == WEEKLY ? "weeks" :
					recurrence == MONTHLY ? "months" : "years");
			if (recurrence_end) {
				char *end_str = convert_timestamp(recurrence_end, 0);
				printf("Until: %s\n", end_str);
				free(end_str);
			}
		}
		free(time_str);
	} else if (strcmp(argv[1], "edit") == 0) {
		const char *e = getenv("EDITOR");
		if (e == NULL) {
			e = editor;
			fprintf(stderr, "$EDITOR not defined, falling back to %s\n", e);
		}
		char *db_path = get_database_path();
		if (!db_path) {
			fprintf(stderr, "Cannot find database path\n");
			return EXIT_FAILURE;
		}
		execlp(e, e, db_path, NULL);
		perror("Failed to spawn editor");
		return EXIT_FAILURE;
	} else if (strcmp(argv[1], "run") == 0) {
		watch_file(events, &num_events);
	} else if (strcmp(argv[1], "help") == 0) {
		usage(0);
	} else {
		fprintf(stderr, "Unknown command: %s\n", argv[1]);
		usage(1);
	}
	return EXIT_SUCCESS;
}
