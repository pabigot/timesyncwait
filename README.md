# Generic systemd support to wait until kernel time synchronized

As discussed in [systemd issue
5097][#5097] [systemd-timesyncd][timesyncd] fails to fulfill the
expectations of the Linux Standard
Base [`$timer` facility][lsb.facilname] and [systemd.timer][]'s
`OnCalendar=` directive because it releases `time-sync.target` before it
has provided the kernel with a synchronized system time.

While it may be technically possible to change `systemd-timesyncd` to
delay releasing `time-sync.target` until the system time has been
synchronized, this has not been done.  More generally, a fix for
`systemd-timesyncd` does not generalize to other non-conforming
implementations that maintain a system time and cannot guarantee
synchronization will be completed by a fixed deadline.

This repository provides a systemd service `timesyncwait` that blocks
until the kernel time has been marked as synchronized with an external
source, at which point it releases `time-synchronized.target`.  Services
that should not be started until system time is synchronized may be
scheduled after that synchronization point.

## Notes

Services released when the `time-synchronized.target` synchronization
point has passed do not necessarily have a synchronized clock.  For
example, this happens if `timesyncwait.service` fails.  Further, it is
possible for clocks to become desynchronized.  Anything that really
cares whether it has a synchronized clock really needs to monitor for
timer set events and check [`STA_UNSYNC`][adjtimex] itself.

[#5097]: https://github.com/systemd/systemd/issues/5097
[timesyncd]: https://www.freedesktop.org/software/systemd/man/systemd-timesyncd.html
[lsb.facilname]: http://refspecs.linuxbase.org/LSB_2.0.1/LSB-PDA/LSB-PDA/facilname.html
[systemd.timer]: https://www.freedesktop.org/software/systemd/man/systemd.timer.html
[adjtimex]: http://man7.org/linux/man-pages/man2/adjtimex.2.html
