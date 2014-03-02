#include "drivers/tty/n_tty.h"

#include "errno.h"

#include "drivers/tty/driver.h"
#include "drivers/tty/ldisc.h"
#include "drivers/tty/tty.h"

#include "mm/kmalloc.h"

#include "proc/kthread.h"

#include "util/debug.h"

/* helpful macros */
#define EOFC            '\x4'
#define TTY_BUF_SIZE    128
#define ldisc_to_ntty(ldisc) \
        CONTAINER_OF(ldisc, n_tty_t, ntty_ldisc)

static void n_tty_attach(tty_ldisc_t *ldisc, tty_device_t *tty);
static void n_tty_detach(tty_ldisc_t *ldisc, tty_device_t *tty);
static int n_tty_read(tty_ldisc_t *ldisc, void *buf, int len);
static const char *n_tty_receive_char(tty_ldisc_t *ldisc, char c);
static const char *n_tty_process_char(tty_ldisc_t *ldisc, char c);

static tty_ldisc_ops_t n_tty_ops = {
        .attach       = n_tty_attach,
        .detach       = n_tty_detach,
        .read         = n_tty_read,
        .receive_char = n_tty_receive_char,
        .process_char = n_tty_process_char
};

struct n_tty {
        kmutex_t            ntty_rlock;
        ktqueue_t           ntty_rwaitq;
        char               *ntty_inbuf;
        int                 ntty_rhead;
        int                 ntty_rawtail;
        int                 ntty_ckdtail;

        tty_ldisc_t         ntty_ldisc;
};


tty_ldisc_t *
n_tty_create()
{
        n_tty_t *ntty = (n_tty_t *)kmalloc(sizeof(n_tty_t));
        if (NULL == ntty) return NULL;
        ntty->ntty_ldisc.ld_ops = &n_tty_ops;
        return &ntty->ntty_ldisc;
}

void
n_tty_destroy(tty_ldisc_t *ldisc)
{
        KASSERT(NULL != ldisc);
        kfree(ldisc_to_ntty(ldisc));
}

/*
 * Initialize the fields of the n_tty_t struct, allocate any memory
 * you will need later, and set the tty_ldisc field of the tty.
 */
void
n_tty_attach(tty_ldisc_t *ldisc, tty_device_t *tty)
{
    /*set tty->tty_ldisc field*/
    tty->tty_ldisc = ldisc;

    /*get the pointer to ntty*/
    struct n_tty *ntty = ldisc_to_ntty(ldisc);

    /*initialize each field*/
    kmutex_init(&ntty->ntty_rlock);
    sched_queue_init(&ntty->ntty_rwaitq);
    ntty->ntty_inbuf = (char *)kmalloc(sizeof(char) * TTY_BUF_SIZE);
    ntty->ntty_rhead = 0;
    ntty->ntty_rawtail = 0;
    ntty->ntty_ckdtail = 0;
}

/*
 * Free any memory allocated in n_tty_attach and set the tty_ldisc
 * field of the tty.
 */
void
n_tty_detach(tty_ldisc_t *ldisc, tty_device_t *tty)
{
    tty->tty_ldisc = NULL;
    
    struct n_tty *ntty = ldisc_to_ntty(ldisc);
    kfree(ntty->ntty_inbuf);

    /*not sure about freeing it*/
    /*n_tty_destroy(ldisc);*/
}

/*
 *Self-defined function to deal with special characters.
 */
int
is_ctrl_d(char c) {
    if (c == 0x04) {
        return 1;
    }
    return 0;
}

int
is_backspace(char c) {
    if (c == 0x08 || c == 0x7f) {
        return 1;
    }
    return 0;
}

int
is_newline(char c) {
    if (c == '\r' || c == '\n') {
        return 1;
    }
    return 0;
}

int
is_eof(char c) {
    if (c == EOFC) {
        return 1;
    }
    return 0;
}
/*
 *Self-defined function to deal with special characters.
 */

/*
 *Self-defined function to deal with index incrementing and decrementing
 */
void
increment(int *n) {
    (*n)++;
    *n = *n % TTY_BUF_SIZE;
}

void
decrement(int *n) {
    if (0 == *n) {
        *n = TTY_BUF_SIZE - 1;
    } else {
        (*n)--;
    }
}

int
convert(int n) {
    if (n < 0) {
        n += TTY_BUF_SIZE;
    } else {
        n %= TTY_BUF_SIZE;
    }
    return n;
}
/*
 *Self-defined function to deal with index incrementing and decrementing
 */

/*
 * Read a maximum of len bytes from the line discipline into buf. If
 * the buffer is empty, sleep until some characters appear. This might
 * be a long wait, so it's best to let the thread be cancellable.
 *
 * Then, read from the head of the buffer up to the tail, stopping at
 * len bytes or a newline character, and leaving the buffer partially
 * full if necessary. Return the number of bytes you read into the
 * buf.

 * In this function, you will be accessing the input buffer, which
 * could be modified by other threads. Make sure to make the
 * appropriate calls to ensure that no one else will modify the input
 * buffer when we are not expecting it.
 *
 * Remember to handle newline characters and CTRL-D, or ASCII 0x04,
 * properly.
 */
int
n_tty_read(tty_ldisc_t *ldisc, void *buf, int len)
{
    dbg(DBG_TERM, "Starting read\n");
    struct n_tty *ntty = ldisc_to_ntty(ldisc);
    char *outbuf = (char *)buf;

    if (ntty->ntty_rhead == ntty->ntty_ckdtail) {
        if (EINTR == sched_cancellable_sleep_on(&ntty->ntty_rwaitq)) {
            /*is cancelled, not handled yet*/
            panic("n_tty_read get cancelled\n");
            return 0;
        }
    }

    /*lock*/

    int i = 0;
    for (i = 0 ; i < len ; i++) {
        /*?ckdtail?*/
        outbuf[i] = ntty->ntty_inbuf[convert(ntty->ntty_rhead)];
        if (is_newline(ntty->ntty_inbuf[convert(ntty->ntty_rhead)])) {
            break;
        }
        if (is_ctrl_d(ntty->ntty_inbuf[convert(ntty->ntty_rhead)])) {
            /*and something else*/
            break;
        }
    }

    outbuf[i] = '\0';
    ntty->ntty_rhead = convert(ntty->ntty_rhead + i);

    /*unlock*/

    return i;
        /*NOT_YET_IMPLEMENTED("DRIVERS: n_tty_read");*/
        /*return 0;*/
}
        /**
         * Read bytes from the line discipline into the buffer.
         *
         * @param ldisc the line discipline
         * @param buf the buffer to read into
         * @param len the maximum number of bytes to read
         * @return the number of bytes read
         */

/*
 * The tty subsystem calls this when the tty driver has received a
 * character. Now, the line discipline needs to store it in its read
 * buffer and move the read tail forward.
 *
 * Special cases to watch out for: backspaces (both ASCII characters
 * 0x08 and 0x7F should be treated as backspaces), newlines ('\r' or
 * '\n'), and full buffers.
 *
 * Return a null terminated string containing the characters which
 * need to be echoed to the screen. For a normal, printable character,
 * just the character to be echoed.
 */
const char *
n_tty_receive_char(tty_ldisc_t *ldisc, char c)
{
    /*lock it?*/
    struct n_tty *ntty = ldisc_to_ntty(ldisc);

    /*backspace*/
    if (is_backspace(c)) {
        decrement(&ntty->ntty_rawtail);
        return "\b \b";
    }
    if (is_newline(c)) {
        return "\n";
    }
    return c + "";
        /*
         *NOT_YET_IMPLEMENTED("DRIVERS: n_tty_receive_char");
         *return NULL;
         */
}
        /**
         * Receive a character and return a string to be echoed to the
         * tty.
         *
         * @param ldisc the line discipline to receive the character
         * @param c the character received
         * @return a null terminated string to be echoed to the tty
         */

/*
 * Process a character to be written to the screen.
 *
 * The only special case is '\r' and '\n'.
 */
const char *
n_tty_process_char(tty_ldisc_t *ldisc, char c)
{
        NOT_YET_IMPLEMENTED("DRIVERS: n_tty_process_char");

        return NULL;
}
        /**
         * Process a character and return a string to be echoed to the
         * tty.
         *
         * @param ldisc the line discipline
         * @param c the character to process
         * @return a null terminated string to be echoed to the tty
         */
