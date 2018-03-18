/*
 * systemd service to wait until kernel realtime clock is synchronized
 *
 * Copyright 2018 Peter A. Bigot
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/timex.h>

#include <systemd/sd-event.h>

#ifndef TFD_TIMER_CANCEL_ON_SET
#define TFD_TIMER_CANCEL_ON_SET (1 << 1)
#endif /* TFD_TIMER_CANCEL_ON_SET */

#define TIME_T_MAX (time_t)((UINTMAX_C(1) << ((sizeof(time_t) << 3) - 1)) - 1)

typedef struct state_s {
  int fd;                       /* non-negative is descriptor from timerfd_create */
  int adjtime_state;            /* return value from last adjtimex(2) call */
  sd_event * event;             /* the systemd event loop object */
  sd_event_source * event_source; /* non-null is the active io event source */
} state_s;

static int
io_handler (sd_event_source * s,
            int fd,
            uint32_t revents,
            void * userdata)
{
  state_s *sp = userdata;
  int rc = 0;
  struct itimerspec its = {
    .it_value.tv_sec = TIME_T_MAX,
  };
  struct timex tx = {0};

  /* Release any event source and timer descriptor held from the last
   * check, then set a timerfd to signal when the clock is set.  Do
   * this before we check synchronized state to avoid a race
   * condition. */
  sp->event_source = sd_event_source_unref(sp->event_source);
  if (0 <= sp->fd) {
    (void) close(sp->fd);
  }
  sp->fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
  if (0 <= sp->fd) {
    rc = timerfd_settime(sp->fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &its, NULL);
  }

  if (0 == rc) {
    /* Fetch the kernel time state.  Synchronization state is encoded
     * in the return value. */
    sp->adjtime_state = rc = adjtimex(&tx);
    if (0 <= rc) {
      fprintf(stderr, "adjtime state %d status %x time %u.%06u\n",
              sp->adjtime_state, tx.status,
              (int)tx.time.tv_sec, (int)tx.time.tv_usec / ((STA_NANO & tx.status) ? 1000 : 1));
    }
  }
  if (TIME_ERROR == sp->adjtime_state) {
    /* Not synchronized.  Do a one-shot wait on the descriptor. */
    rc = sd_event_add_io(sp->event, &sp->event_source, sp->fd,
                         EPOLLIN, io_handler, sp);
  } else if (0 <= sp->adjtime_state) {
    /* Synchronized; we can exit. */
    sd_event_exit(sp->event, 0);
    return 0;
  }
  if (0 > rc) {
    /* Something wrong, exit with error */
    sd_event_exit(sp->event, rc);
  }
  return rc;
}

int
main (int argc,
      char * argv[])
{
  int rc;
  state_s state = {
    .fd = -1,
  };

  rc = sd_event_default(&state.event);
  if (0 <= rc) {
    sigset_t ss;
    if ((0 > sigemptyset(&ss))
        || (0 > sigaddset(&ss, SIGTERM))
        || (0 > sigaddset(&ss, SIGINT))
        || (0 > sigprocmask(SIG_BLOCK, &ss, NULL))) {
      rc = -errno;
    }
  }
  if ((0 <= rc)
      && (0 <= ((rc = sd_event_add_signal(state.event, NULL, SIGTERM, NULL, NULL))))
      && (0 <= ((rc = sd_event_add_signal(state.event, NULL, SIGINT, NULL, NULL))))
      && (0 <= ((rc = sd_event_set_watchdog(state.event, true))))
      && (0 <= ((rc = io_handler(0, -1, 0, &state))))
      && (TIME_ERROR == state.adjtime_state)) {
    rc = sd_event_loop(state.event);
    if (0 > rc) {
      fprintf(stderr, "event loop terminated with %d\n", rc);
    } else if (TIME_ERROR == state.adjtime_state) {
      fprintf(stderr, "event loop terminated without synchronizing\n");
      rc = -1;
    }
  }

  (void)close(state.fd);
  state.event_source = sd_event_source_unref(state.event_source);
  state.event = sd_event_unref(state.event);
  return (0 > rc) ? EXIT_FAILURE : EXIT_SUCCESS;
}
