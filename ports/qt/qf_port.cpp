/// @file
/// @brief QP/C++ port to Qt
/// @cond
///***************************************************************************
/// Last updated for version 6.7.0 / Qt 5.x
/// Last updated on  2019-12-26
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

#include <QCoreApplication>
#include "aothread.hpp"
#include "tickerthread.hpp"
//-----------------
#define QP_IMPL             // this is QP implementation
#include "qf_port.hpp"      // QF port
#include "qf_pkg.hpp"       // QF package-scope interface
#include "qassert.h"        // QP embedded systems-friendly assertions
#ifdef Q_SPY                // QS software tracing enabled?
    #include "qs_port.hpp"  // include QS port
#else
    #include "qs_dummy.hpp" // disable the QS software tracing
#endif // Q_SPY

Q_DEFINE_THIS_MODULE("qf_port")

//****************************************************************************
namespace QP {

//............................................................................
QMutex QF_qtMutex_;

//............................................................................
static TickerThread l_tickerThread;

//............................................................................
void QF_enterCriticalSection_() { QF_qtMutex_.lock();   }
void QF_leaveCriticalSection_() { QF_qtMutex_.unlock(); }

//............................................................................
AOThread::~AOThread() {
    wait();
}
//............................................................................
void AOThread::run() {
    Q_REQUIRE(m_act != static_cast<void *>(0));
    QP::QF::thread_(static_cast<QP::QActive *>(m_act));
}

//****************************************************************************
TickerThread::~TickerThread() {
    wait();
}
//............................................................................
void TickerThread::run() {
    m_isRunning = true;
    do {
        msleep(m_tickInterval);
        QP::QF_onClockTick();
#ifdef Q_SPY
        QP::QS_onEvent();
#endif
    } while (m_isRunning);
}

//............................................................................
void QF::init(void) {
}
//............................................................................
int_t QF::run(void) {
    onStartup(); // invoke the startup callback

    //l_tickerThread.setStackSize(1024U*4U); // 4KB of stack
    l_tickerThread.start();

    // run the Qt event loop (console or GUI)
    return static_cast<int_t>(QCoreApplication::instance()->exec());
}
//............................................................................
void QF::thread_(QActive *act) {
    AOThread *thread = static_cast<AOThread *>(act->m_thread);
    thread->m_isRunning = true;

    // event-loop
    for (;;) { // for-ever
        QEvt const *e = act->get_(); // wait for event
        act->dispatch(e); // dispatch to the active object's state machine
        gc(e); // check if the event is garbage, and collect it if so
    }
}
//............................................................................
void QF::stop(void) {
    l_tickerThread.m_isRunning = false;
}
//............................................................................
void QF_setQtPrio(QActive *act, int_t qtPrio) {
    // thread not created yet?
    if (act->getThread() == static_cast<QThread *>(0)) {
        // store the priority for later
        act->getOsObject() = reinterpret_cast<QWaitCondition *>(qtPrio);
    }
    else {
        act->getThread()->setPriority(static_cast<QThread::Priority>(qtPrio));
    }
}
//............................................................................
void QF_setTickRate(unsigned ticksPerSec) {
    l_tickerThread.m_tickInterval = 1000U/ticksPerSec;
}
//............................................................................
void QActive::start(uint_fast8_t const prio,
                    QEvt const * * const qSto, uint_fast16_t const qLen,
                    void * const stkSto, uint_fast16_t const stkSize,
                    void const * const par)
{
    Q_REQUIRE(stkSto == static_cast<void *>(0)); // no per-task stack

    m_thread   = new AOThread(this);
    m_osObject = new QWaitCondition;
    m_eQueue.init(qSto, qLen);
    m_prio = prio;

    QF::add_(this); // make QF aware of this active object

    init(par);      // execute the initial transition
    QS_FLUSH();     // flush the trace buffer to the host

    AOThread *thread = static_cast<AOThread *>(m_thread);
    thread->setStackSize(stkSize);
    thread->start();
}

} // namespace QP
