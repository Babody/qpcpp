/// @file
/// @brief QF/C++ port to POSIX/P-threads
/// @cond
///***************************************************************************
/// Last updated for version 6.7.0
/// Last updated on  2019-12-28
///
///                    Q u a n t u m  L e a P s
///                    ------------------------
///                    Modern Embedded Software
///
/// Copyright (C) 2005-2019 Quantum Leaps. All rights reserved.
///
/// This program is open source software: you can redistribute it and/or
/// modify it under the terms of the GNU General Public License as published
/// by the Free Software Foundation, either version 3 of the License, or
/// (at your option) any later version.
///
/// Alternatively, this program may be distributed and modified under the
/// terms of Quantum Leaps commercial licenses, which expressly supersede
/// the GNU General Public License and are specifically designed for
/// licensees interested in retaining the proprietary status of their code.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program. If not, see <www.gnu.org/licenses>.
///
/// Contact information:
/// <www.state-machine.com/licensing>
/// <info@state-machine.com>
///***************************************************************************
/// @endcond
///

// expose features from the 2008 POSIX standard (IEEE Standard 1003.1-2008)
#define _POSIX_C_SOURCE 200809L

#define QP_IMPL             // this is QP implementation
#include "qf_port.hpp"      // QF port
#include "qf_pkg.hpp"       // QF package-scope interface
#include "qassert.h"        // QP embedded systems-friendly assertions
#ifdef Q_SPY                // QS software tracing enabled?
    #include "qs_port.hpp"  // include QS port
#else
    #include "qs_dummy.hpp" // disable the QS software tracing
#endif // Q_SPY

#include <limits.h>         // for PTHREAD_STACK_MIN
#include <sys/mman.h>       // for mlockall()
#include <sys/select.h>
#include <sys/ioctl.h>
#include <string.h>         // for memcpy() and memset()
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

namespace QP {

Q_DEFINE_THIS_MODULE("qf_port")

/* Global objects ==========================================================*/
pthread_mutex_t QF_pThreadMutex_; // mutex for QF critical section

// Local objects *************************************************************
static pthread_mutex_t l_startupMutex;
static bool l_isRunning;      // flag indicating when QF is running
static struct termios l_tsav; // structure with saved terminal attributes
static struct timespec l_tick;
static int_t l_tickPrio;
enum { NANOSLEEP_NSEC_PER_SEC = 1000000000 }; // see NOTE05

static void sigIntHandler(int /* dummy */);
static void *ao_thread(void *arg); // thread routine for all AOs

// QF functions ==============================================================
void QF::init(void) {
    // lock memory so we're never swapped out to disk
    //mlockall(MCL_CURRENT | MCL_FUTURE); // uncomment when supported

    // init the global mutex with the default non-recursive initializer
    pthread_mutex_init(&QF_pThreadMutex_, NULL);

    // init the startup mutex with the default non-recursive initializer
    pthread_mutex_init(&l_startupMutex, NULL);

    // lock the startup mutex to block any active objects started before
    // calling QF::run()
    pthread_mutex_lock(&l_startupMutex);

    // clear the internal QF variables, so that the framework can (re)start
    // correctly even if the startup code is not called to clear the
    // uninitialized data (as is required by the C++ Standard).
    extern uint_fast8_t QF_maxPool_;
    QF_maxPool_ = static_cast<uint_fast8_t>(0);
    bzero(&QF::timeEvtHead_[0],
          static_cast<uint_fast16_t>(sizeof(QF::timeEvtHead_)));
    bzero(&active_[0], static_cast<uint_fast16_t>(sizeof(active_)));

    l_tick.tv_sec = 0;
    l_tick.tv_nsec = NANOSLEEP_NSEC_PER_SEC/100L; // default clock tick
    l_tickPrio = sched_get_priority_min(SCHED_FIFO); // default tick prio

    // install the SIGINT (Ctrl-C) signal handler
    struct sigaction sig_act;
    sig_act.sa_handler = &sigIntHandler;
    sigaction(SIGINT, &sig_act, NULL);
}

//****************************************************************************
void QF_enterCriticalSection_(void) {
    pthread_mutex_lock(&QF_pThreadMutex_);
}
//****************************************************************************
void QF_leaveCriticalSection_(void) {
    pthread_mutex_unlock(&QF_pThreadMutex_);
}

//****************************************************************************
int_t QF::run(void) {
    onStartup(); // application-specific startup callback

    // try to set the priority of the ticker thread, see NOTE01
    struct sched_param sparam;
    sparam.sched_priority = l_tickPrio;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sparam) == 0) {
        // success, this application has sufficient privileges
    }
    else {
        // setting priority failed, probably due to insufficient privieges
    }

    // unlock the startup mutex to unblock any active objects started before
    // calling QF::run()
    pthread_mutex_unlock(&l_startupMutex);

    l_isRunning = true;
    while (l_isRunning) { // the clock tick loop...
        QF_onClockTick(); // clock tick callback (must call QF_TICK_X())

        nanosleep(&l_tick, NULL); // sleep for the number of ticks, NOTE05
    }
    onCleanup(); // cleanup callback
    pthread_mutex_destroy(&l_startupMutex);
    pthread_mutex_destroy(&QF_pThreadMutex_);

    return static_cast<int_t>(0); // return success
}
//****************************************************************************
void QF_setTickRate(uint32_t ticksPerSec, int_t tickPrio) {
    Q_REQUIRE_ID(300, ticksPerSec != static_cast<uint32_t>(0));
    l_tick.tv_nsec = NANOSLEEP_NSEC_PER_SEC / ticksPerSec;
    l_tickPrio = tickPrio;
}
//............................................................................
void QF::stop(void) {
    l_isRunning = false; // stop the loop in QF::run()
}
//............................................................................
void QF::thread_(QActive *act) {
    // block this thread until the startup mutex is unlocked from QF::run()
    pthread_mutex_lock(&l_startupMutex);
    pthread_mutex_unlock(&l_startupMutex);

    // event-loop
    for (;;) { // for-ever
        QEvt const *e = act->get_(); // wait for event
        act->dispatch(e); // dispatch to the active object's state machine
        gc(e); // check if the event is garbage, and collect it if so
    }
}

//............................................................................
void QF_consoleSetup(void) {
    struct termios tio;   // modified terminal attributes

    tcgetattr(0, &l_tsav); // save the current terminal attributes
    tcgetattr(0, &tio);    // obtain the current terminal attributes
    tio.c_lflag &= ~(ICANON | ECHO); // disable the canonical mode & echo
    tcsetattr(0, TCSANOW, &tio);     // set the new attributes
}
//............................................................................
void QF_consoleCleanup(void) {
    tcsetattr(0, TCSANOW, &l_tsav); // restore the saved attributes
}
//............................................................................
int QF_consoleGetKey(void) {
    int byteswaiting;
    ioctl(0, FIONREAD, &byteswaiting);
    if (byteswaiting > 0) {
        char ch;
        (void)read(0, &ch, 1);
        return (int)ch;
    }
    return 0; // no input at this time
}
/*..........................................................................*/
int QF_consoleWaitForKey(void) {
    return getchar();
}

//****************************************************************************
void QActive::start(uint_fast8_t const prio,
                    QEvt const * * const qSto, uint_fast16_t const qLen,
                    void * const stkSto, uint_fast16_t const stkSize,
                    void const * const par)
{
    // p-threads allocate stack internally
    Q_REQUIRE_ID(600, stkSto == static_cast<void *>(0));

    pthread_cond_init(&m_osObject, 0);

    m_eQueue.init(qSto, qLen);
    m_prio = static_cast<uint8_t>(prio); // set the QF priority of this AO
    QF::add_(this); // make QF aware of this AO

    this->init(par); // execute initial transition (virtual call)
    QS_FLUSH(); // flush the QS trace buffer to the host

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    // SCHED_FIFO corresponds to real-time preemptive priority-based scheduler
    // NOTE: This scheduling policy requires the superuser privileges
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);

    // see NOTE04
    struct sched_param param;
    param.sched_priority = prio
                           + (sched_get_priority_max(SCHED_FIFO)
                              - QF_MAX_ACTIVE - 3);

    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // stack size not provided?
    if (stkSize == 0U) {
        // set the allowed minimum
        stkSize = (uint_fast16_t)PTHREAD_STACK_MIN;
    }
    pthread_t thread;
    if (pthread_create(&thread, &attr, &ao_thread, this) != 0) {

        // Creating the p-thread with the SCHED_FIFO policy failed.
        // Most probably this application has no superuser privileges,
        // so we just fall back to the default SCHED_OTHER policy
        // and priority 0.

        pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
        param.sched_priority = 0;
        pthread_attr_setschedparam(&attr, &param);
        Q_ALLEGE_ID(601,
            pthread_create(&thread, &attr, &ao_thread, this) == 0);
    }
    pthread_attr_destroy(&attr);
    m_thread = static_cast<uint8_t>(1);
}

//............................................................................
static void *ao_thread(void *arg) { // the expected POSIX signature
    QF::thread_(static_cast<QActive *>(arg));
    return static_cast<void *>(0); // return success
}

//****************************************************************************
static void sigIntHandler(int /* dummy */) {
    QF::onCleanup();
    exit(-1);
}

} // namespace QP

//****************************************************************************
// NOTE01:
// In Linux, the scheduler policy closest to real-time is the SCHED_FIFO
// policy, available only with superuser privileges. QF::run() attempts to set
// this policy as well as to maximize its priority, so that the ticking
// occurrs in the most timely manner (as close to an interrupt as possible).
// However, setting the SCHED_FIFO policy might fail, most probably due to
// insufficient privileges.
//
// NOTE02:
// On some Linux systems nanosleep() might actually not deliver the finest
// time granularity. For example, on some Linux implementations, nanosleep()
// could not block for shorter intervals than 20ms, while the underlying
// clock tick period was only 10ms. Sometimes, the select() system call can
// provide a finer granularity.
//
// NOTE03:
// Any blocking system call, such as nanosleep() or select() system call can
// be interrupted by a signal, such as ^C from the keyboard. In this case this
// QF port breaks out of the event-loop and returns to main() that exits and
// terminates all spawned p-threads.
//
// NOTE04:
// According to the man pages (for pthread_attr_setschedpolicy) the only value
// supported in the Linux p-threads implementation is PTHREAD_SCOPE_SYSTEM,
// meaning that the threads contend for CPU time with all processes running on
// the machine. In particular, thread priorities are interpreted relative to
// the priorities of all other processes on the machine.
//
// This is good, because it seems that if we set the priorities high enough,
// no other process (or thread running within) can gain control over the CPU.
//
// However, QF limits the number of priority levels to QF_MAX_ACTIVE.
// Assuming that a QF application will be real-time, this port reserves the
// three highest Linux priorities for the ISR-like threads (e.g., the ticker,
// I/O), and the rest highest-priorities for the active objects.
//
// NOTE05:
// In some (older) Linux kernels, the POSIX nanosleep() system call might
// deliver only 2*actual-system-tick granularity. To compensate for this,
// you would need to reduce (by 2) the constant NANOSLEEP_NSEC_PER_SEC.
//

