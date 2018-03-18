prefix ?= /

timesyncwait: timesyncwait.c
	$(CC) -o $@ $^ -lsystemd

install: timesyncwait
	systemctl disable timesyncwait || true
	install -m755 \
	  timesyncwait \
	  $(prefix)/lib/systemd
	install -m644 \
	  timesyncwait.service \
	  time-synchronized.target \
	  $(prefix)/lib/systemd/system
	systemctl daemon-reload
	systemctl enable timesyncwait
