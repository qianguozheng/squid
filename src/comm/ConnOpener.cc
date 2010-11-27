/*
 * DEBUG: section 05    Socket Connection Opener
 */

#include "config.h"
//#include "base/TextException.h"
#include "comm/ConnOpener.h"
#include "comm/Connection.h"
#include "comm.h"
#include "fde.h"
#include "icmp/net_db.h"
#include "SquidTime.h"

namespace Comm {
    CBDATA_CLASS_INIT(ConnOpener);
};

Comm::ConnOpener::ConnOpener(Comm::ConnectionPointer &c, AsyncCall::Pointer &handler, time_t ctimeout) :
        AsyncJob("Comm::ConnOpener"),
        host_(NULL),
        conn_(c),
        callback_(handler),
        totalTries_(0),
        failRetries_(0),
        connectTimeout_(ctimeout),
        connectStart_(0)
{}

Comm::ConnOpener::~ConnOpener()
{
    safe_free(host_);
}

bool
Comm::ConnOpener::doneAll() const
{
    // is the conn_ to be opened still waiting?
    if (conn_ != NULL) {
        return false;
    }

    // is the callback still to be called?
    if (callback_ != NULL) {
        return false;
    }

    return AsyncJob::doneAll();
}

void
Comm::ConnOpener::swanSong()
{
    // cancel any event watchers
    // done here to get the "swanSong" mention in cancel debugging.
    if (calls_.earlyAbort_ != NULL) {
        calls_.earlyAbort_->cancel("Comm::ConnOpener::swanSong");
        calls_.earlyAbort_ = NULL;
    }
    if (calls_.timeout_ != NULL) {
        calls_.timeout_->cancel("Comm::ConnOpener::swanSong");
        calls_.timeout_ = NULL;
    }

    // rollback what we can from the job state
    if (conn_ != NULL && conn_->isOpen()) {
        // drop any handlers now to save a lot of cycles later
        commSetSelect(conn_->fd, COMM_SELECT_WRITE, NULL, NULL, 0);
        commSetTimeout(conn_->fd, -1, NULL, NULL);
        // it never reached fully open, so abort the FD
        conn_->close();
    }

    if (callback_ != NULL) {
        // inform the still-waiting caller we are dying
        doneConnecting(COMM_ERR_CONNECT, 0);
    }

    AsyncJob::swanSong();
}

void
Comm::ConnOpener::setHost(const char * new_host)
{
    // unset and erase if already set.
    if (host_ != NULL)
        safe_free(host_);

    // set the new one if given.
    if (new_host != NULL)
        host_ = xstrdup(new_host);
}

const char *
Comm::ConnOpener::getHost() const
{
    return host_;
}

/**
 * Connection attempt are completed. One way or the other.
 * Pass the results back to the external handler.
 */
void
Comm::ConnOpener::doneConnecting(comm_err_t status, int xerrno)
{
    if (callback_ != NULL) {
        typedef CommConnectCbParams Params;
        Params &params = GetCommParams<Params>(callback_);
        params.conn = conn_;
        params.flag = status;
        params.xerrno = xerrno;
        ScheduleCallHere(callback_);
        callback_ = NULL;
    }

    /* ensure cleared local state, we are done. */
    conn_ = NULL;
}

void
Comm::ConnOpener::start()
{
    Must(conn_ != NULL);

    /* get a socket open ready for connecting with */
    if (!conn_->isOpen()) {
#if USE_IPV6
        /* outbound sockets have no need to be protocol agnostic. */
        if (conn_->remote.IsIPv4()) {
            conn_->local.SetIPv4();
        }
#endif
        conn_->fd = comm_openex(SOCK_STREAM, IPPROTO_TCP, conn_->local, conn_->flags, conn_->tos, conn_->nfmark, host_);
        if (!conn_->isOpen()) {
            doneConnecting(COMM_ERR_CONNECT, 0);
            return;
        }
    }

    typedef CommCbMemFunT<Comm::ConnOpener, CommConnectCbParams> abortDialer;
    calls_.earlyAbort_ = JobCallback(5, 4, abortDialer, this, Comm::ConnOpener::earlyAbort);
    comm_add_close_handler(conn_->fd, calls_.earlyAbort_);

    typedef CommCbMemFunT<Comm::ConnOpener, CommTimeoutCbParams> timeoutDialer;
    calls_.timeout_ = JobCallback(5, 4, timeoutDialer, this, Comm::ConnOpener::timeout);
    debugs(5, 3, HERE << conn_ << " timeout " << connectTimeout_);
    commSetTimeout(conn_->fd, connectTimeout_, calls_.timeout_);

    connectStart_ = squid_curtime;
    connect();
}

void
Comm::ConnOpener::connected()
{
    /*
     * stats.conn_open is used to account for the number of
     * connections that we have open to the peer, so we can limit
     * based on the max-conn option.  We need to increment here,
     * even if the connection may fail.
     */
    if (conn_->getPeer())
        conn_->getPeer()->stats.conn_open++;

    lookupLocalAddress();

    /* TODO: remove these fd_table accesses. But old code still depends on fd_table flags to
     *       indicate the state of a raw fd object being passed around.
     *       Also, legacy code still depends on comm_local_port() with no access to Comm::Connection
     *       when those are done comm_local_port can become one of our member functions to do the below.
     */
    fd_table[conn_->fd].flags.open = 1;
    fd_table[conn_->fd].local_addr = conn_->local;
}

/** Make an FD connection attempt.
 * Handles the case(s) when a partially setup connection gets closed early.
 */
void
Comm::ConnOpener::connect()
{
    Must(conn_ != NULL);

    totalTries_++;

    switch (comm_connect_addr(conn_->fd, conn_->remote) ) {

    case COMM_INPROGRESS:
        // check for timeout FIRST.
        if (squid_curtime - connectStart_ > connectTimeout_) {
            debugs(5, 5, HERE << conn_ << ": * - ERR took too long already.");
            conn_->close();
            doneConnecting(COMM_TIMEOUT, errno);
            return;
        } else {
            debugs(5, 5, HERE << conn_ << ": COMM_INPROGRESS");
            commSetSelect(conn_->fd, COMM_SELECT_WRITE, Comm::ConnOpener::InProgressConnectRetry, this, 0);
        }
        break;

    case COMM_OK:
        debugs(5, 5, HERE << conn_ << ": COMM_OK - connected");

        connected();

        if (host_ != NULL)
            ipcacheMarkGoodAddr(host_, conn_->remote);
        doneConnecting(COMM_OK, 0);
        break;

    default:
        debugs(5, 5, HERE << conn_ << ": * - try again");
        failRetries_++;
        if (host_ != NULL)
            ipcacheMarkBadAddr(host_, conn_->remote);
#if USE_ICMP
        if (Config.onoff.test_reachability)
            netdbDeleteAddrNetwork(conn_->remote);
#endif

        // check for timeout FIRST.
        if(squid_curtime - connectStart_ > connectTimeout_) {
            debugs(5, 5, HERE << conn_ << ": * - ERR took too long already.");
            if (calls_.earlyAbort_ != NULL) {
                calls_.earlyAbort_->cancel("Comm::ConnOpener::connect timed out");
                calls_.earlyAbort_ = NULL;
            }
            conn_->close();
            doneConnecting(COMM_TIMEOUT, errno);
        } else if (failRetries_ < Config.connect_retries) {
            eventAdd("Comm::ConnOpener::DelayedConnectRetry", Comm::ConnOpener::DelayedConnectRetry, this, 0.05, 0);
        } else {
            // send ERROR back to the upper layer.
            debugs(5, 5, HERE << conn_ << ": * - ERR tried too many times already.");
            if (calls_.earlyAbort_ != NULL) {
                calls_.earlyAbort_->cancel("Comm::ConnOpener::connect failed");
                calls_.earlyAbort_ = NULL;
            }
            conn_->close();
            doneConnecting(COMM_ERR_CONNECT, errno);
        }
    }
}

/**
 * Lookup local-end address and port of the TCP link just opened.
 * This ensure the connection local details are set correctly
 */
void
Comm::ConnOpener::lookupLocalAddress()
{
    struct addrinfo *addr = NULL;
    conn_->local.InitAddrInfo(addr);

    if (getsockname(conn_->fd, addr->ai_addr, &(addr->ai_addrlen)) != 0) {
        debugs(50, DBG_IMPORTANT, "ERROR: Failed to retrieve TCP/UDP details for socket: " << conn_ << ": " << xstrerror());
        conn_->local.FreeAddrInfo(addr);
        return;
    }

    conn_->local = *addr;
    conn_->local.FreeAddrInfo(addr);
    debugs(5, 6, HERE << conn_);
}

/** Abort connection attempt.
 * Handles the case(s) when a partially setup connection gets closed early.
 */
void
Comm::ConnOpener::earlyAbort(const CommConnectCbParams &io)
{
    debugs(5, 3, HERE << io.conn);
    doneConnecting(COMM_ERR_CLOSING, io.xerrno); // NP: is closing or shutdown better?
}

/**
 * Handles the case(s) when a partially setup connection gets timed out.
 * NP: When commSetTimeout accepts generic CommCommonCbParams this can die.
 */
void
Comm::ConnOpener::timeout(const CommTimeoutCbParams &)
{
    connect();
}

/* Legacy Wrapper for the retry event after COMM_INPROGRESS
 * XXX: As soon as comm commSetSelect() accepts Async calls we can use a ConnOpener::connect call
 */
void
Comm::ConnOpener::InProgressConnectRetry(int fd, void *data)
{
    ConnOpener *cs = static_cast<Comm::ConnOpener *>(data);
    assert(cs);

    // Ew. we are now outside the all AsyncJob protections.
    // get back inside by scheduling another call...
    typedef NullaryMemFunT<Comm::ConnOpener> Dialer;
    AsyncCall::Pointer call = JobCallback(5, 4, Dialer, cs, Comm::ConnOpener::connect);
    ScheduleCallHere(call);
}

/* Legacy Wrapper for the retry event with small delay after errors.
 * XXX: As soon as eventAdd() accepts Async calls we can use a ConnOpener::connect call
 */
void
Comm::ConnOpener::DelayedConnectRetry(void *data)
{
    ConnOpener *cs = static_cast<Comm::ConnOpener *>(data);
    assert(cs);

    // Ew. we are now outside the all AsyncJob protections.
    // get back inside by scheduling another call...
    typedef NullaryMemFunT<Comm::ConnOpener> Dialer;
    AsyncCall::Pointer call = JobCallback(5, 4, Dialer, cs, Comm::ConnOpener::connect);
    ScheduleCallHere(call);
}
