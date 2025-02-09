# ssm 
Simple Schedule Manager(ssm) is a highly scriptable scheduler. It supports recurring events and support daily, weekly, monthly events.

# Usage
```
Simple Scheduler Manager 1.0.0

Usage: ssm <command>

	help						Show this help message
	sched <time> <title> <description> [options]	Schedule an event
	edit						Edit schedule with $EDITOR
	list						List all upcoming events
	run						Spawn notifier daemon
```
# Dependencies
None

# Building
You will need to run these with elevated privilages.
```
$ make
# make install
```

# Contributions
Contributions are welcomed, feel free to open a pull request.

# License
This project is licensed under the GNU Public License v3.0. See [LICENSE](https://github.com/night0721/ssm/blob/master/LICENSE) for more information.
