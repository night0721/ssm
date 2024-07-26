# ssm 
Simple Schedule Manager(ssm) is a highly scriptable scheduler.

# Usage
```
Simple Scheduler Manager 1.0.0

Usage: ssm <command>

	help				Show this help message
	sched <time> <title> [description]	Schedule an event
	edit					Edit schedule with $EDITOR
	list <timerange>			List all upcoming events
	search					Search for events
	run					Spawn notifier daemon
```
# Dependencies
- libnotify

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
