#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/inotify.h>

#include <libnotify/notify.h>

#include "ssm.h"

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_LINE_LEN 4096

typedef struct {
	time_t timestamp;
	int alert; /* in ms */
	char name[50];
	char description[100];
	int notified;
} event;

char *
get_database_path(void)
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
		
		/* replace ~ with home */
		snprintf(db_path, strlen(db) + strlen(home), "%s%s", home, db + 1);
	} else {
		db_path = strdup(db);
	}
	return db_path;
}

typedef enum {
	FULL, // YYYY-MM-DD HH:MM:SS
	HHMM, // HH:MM
} datefmt;

char *
convert_timestamp(time_t timestamp, datefmt format)
{
	struct tm *time_info = localtime(&timestamp);
	char *time_buf = malloc(20 * sizeof(char));
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

void
load_events(event **events, int *num_events)
{
	char *db_path = get_database_path();
	if (db_path == NULL)
		return;
	FILE *file = fopen(db_path, "r");
	if (!file) {
		fprintf(stderr, "Cannot open database file: %s\n", db_path);
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
		sscanf(line, "%ld\t%[^\t]\t%[^\n]", &(*events)[i].timestamp, (*events)[i].name, (*events)[i].description);
	}

	fclose(file);
	free(db_path);
}

void
add_event(time_t timestamp, char *name, char *description)
{
	char *db_path = get_database_path();
	FILE *file;
	file = fopen(db_path, access(db_path, F_OK) ? "w" : "a");
	if (!file) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}

	fprintf(file, "%ld\t%s\t%s\n", timestamp, name, description);
	fclose(file);
	free(db_path);
	char *time_buf = convert_timestamp(timestamp, 0);
	printf("Added \"%s\" with description \"%s\" at \"%s\"\n", name, description, time_buf);
	free(time_buf);
}

static void
send_notification(event to_alert)
{
	int name_len = strlen(to_alert.name);
	char name_buf[name_len + 7]; /* 7 for "ssm - " and NULL*/
	snprintf(name_buf, name_len + 7, "ssm - %s", to_alert.name);

	int description_len = strlen(to_alert.description);
	char *time_buf = convert_timestamp(to_alert.timestamp, 1);
	char description_buf[description_len + 6 + 3]; /* 1 space, 1 comma, 1 NULL */
	snprintf(description_buf, description_len + 9, "%s, %s", to_alert.description, time_buf);

	free(time_buf);
	NotifyNotification *notification = notify_notification_new(name_buf, description_buf, "dialog-information");

	if (notification == NULL) {
		perror("notify_notification_new");
	}
	if (!notify_notification_show(notification, NULL)) {
		perror("notify_notification_show");
	}
	g_object_unref(G_OBJECT(notification));
}

void 
check_events(event *events, int *num_events)
{
	time_t now = time(NULL);
	for (int i = 0; i < *num_events; i++) {
		if (events[i].timestamp > now && events[i].notified == 0) {
			send_notification(events[i]);
			events[i].notified = 1;
		}
	}
}

void
list_events(event *events, int *num_events)
{
	load_events(&events, num_events);
	for (int i = 0; i < *num_events; i++) {
		printf("Timestamp: %ld\nName: %s\nDescription: %s\n\n", events[i].timestamp, events[i].name, events[i].description);
	}
	free(events);
}

void
watch_file(event *events, int *num_events)
{
	if (notify_init("ssm") < 0) {
		perror("notify_init");
		exit(EXIT_FAILURE);
	}
	int inotify_fd = inotify_init1(IN_NONBLOCK);
	if (inotify_fd == -1) {
		perror("inotify_init1");
		exit(EXIT_FAILURE);
	}

	char *db_path = get_database_path();
	int wd = inotify_add_watch(inotify_fd, db_path, IN_MODIFY);
	free(db_path);
	if (wd == -1) {
		perror("inotify_add_watch");
		exit(EXIT_FAILURE);
	}

	char buf[BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));

	load_events(&events, num_events);
	check_events(events, num_events);
	free(events);

	while (1) {
		ssize_t len = read(inotify_fd, buf, BUF_LEN);
		if (len == -1 && errno != EAGAIN) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		if (len <= 0) {
			sleep(1);
			continue;
		}
		/* There is modification */
		load_events(&events, num_events);
		check_events(events, num_events);
		free(events);

		for (char *ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + ((struct inotify_event *) ptr)->len) {
			struct inotify_event *event = (struct inotify_event *) ptr;
			if (event->mask & IN_MODIFY) {
				printf("Detected modification in database file\n");
			}
		}
	}

	close(inotify_fd);

}

static _Noreturn void
usage(int code)
{
	fprintf(code ? stderr : stdout,
			"Simple Scheduler Manager " VERSION "\n\n"
			"Usage: ssm <command>\n\n"
			"	help					Show this help message\n"
			"	sched <time> <title> [description]	Schedule an event\n"
			"	edit					Edit schedule with $EDITOR\n"
			"	list <timerange>			List all upcoming events\n"
			"	search					Search for events\n"
			"	run					Spawn notifier daemon\n"
			);
	exit(code);
}

int
main(int argc, char **argv)
{
	event *events = NULL;
	int num_events = 0;
	if (argc == 1) {
		char *time_buf = convert_timestamp(time(NULL), 0);
		printf("Current date: %s\n", time_buf);
		printf("Upcoming events:\n");
		list_events(events, &num_events);
		free(time_buf);
		return EXIT_SUCCESS;
	}
	if (strcmp(argv[1], "sched") == 0) {
		/* time can be relative or absolute */
		char *when = argv[2];
		char *name = argv[3];
		char *description = argv[4];
		/* todo: accept time */
		add_event(time(NULL) + 60, name, description);
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
	} else if (strcmp(argv[1], "list") == 0) {
		/* accept argv[2] as timerange */
		list_events(events, &num_events);
	} else if (strcmp(argv[1], "search") == 0) {

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
