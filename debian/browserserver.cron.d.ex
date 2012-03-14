#
# Regular cron jobs for the browserserver package
#
0 4	* * *	root	[ -x /usr/bin/browserserver_maintenance ] && /usr/bin/browserserver_maintenance
