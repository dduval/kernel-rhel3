/*
 * TUX - Integrated Application Protocols Layer and Object Cache
 *
 * Copyright (C) 2000, 2001, Ingo Molnar <mingo@redhat.com>
 *
 * accept.c: accept new connections, allocate requests
 */

#include <net/tux.h>

/****************************************************************
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2, or (at your option)
 *      any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

unsigned int tux_ack_pingpong = 1;
unsigned int tux_push_all = 0;
unsigned int tux_zerocopy_parse = 1;

static int __idle_event (tux_req_t *req);
static int __output_space_event (tux_req_t *req);

struct socket * start_listening(tux_socket_t *listen, int nr)
{
	struct sockaddr_in sin;
	struct socket *sock = NULL;
	struct sock *sk;
	struct tcp_opt *tp;
	int err;
	u16 port = listen->port;
	u32 addr = listen->ip;
	tux_proto_t *proto = listen->proto;

	/* Create a listening socket: */

	err = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (err < 0) {
		printk(KERN_ERR "TUX: error %d creating socket.\n", err);
		goto err;
	}

	/* Bind the socket: */

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(addr);
	sin.sin_port = htons(port);

	sk = sock->sk;
	sk->reuse = 1;
	sk->urginline = 1;

	err = sock->ops->bind(sock, (struct sockaddr*)&sin, sizeof(sin));
	if (err < 0) {
		printk(KERN_ERR "TUX: error %d binding socket. This means that probably some other process is (or was a short time ago) using addr %s://%d.%d.%d.%d:%d.\n", 
			err, proto->name, HIPQUAD(addr), port);
		goto err;
	}

	tp = &sk->tp_pinfo.af_tcp;
	tp->ack.pingpong = tux_ack_pingpong;

	sk->linger = 0;
	sk->lingertime = 0;
	tp->linger2 = tux_keepalive_timeout * HZ;

	if (proto->defer_accept && !tux_keepalive_timeout && tux_defer_accept)
		tp->defer_accept = 1;

	/* Now, start listening on the socket */

	err = sock->ops->listen(sock, tux_max_backlog);
	if (err) {
		printk(KERN_ERR "TUX: error %d listening on socket.\n", err);
		goto err;
	}

	printk(KERN_NOTICE "TUX: thread %d listens on %s://%d.%d.%d.%d:%d.\n",
		nr, proto->name, HIPQUAD(addr), port);
	return sock;

err:
	if (sock)
		sock_release(sock);
	return NULL;
}

static inline void __kfree_req (tux_req_t *req, threadinfo_t * ti)
{
	list_del(&req->all);
	DEBUG_DEL_LIST(&req->all);
	ti->nr_requests--;
	kfree(req);
}

int flush_freequeue (threadinfo_t * ti)
{
	struct list_head *tmp;
	unsigned long flags;
	tux_req_t *req;
	int count = 0;

	spin_lock_irqsave(&ti->free_requests_lock,flags);
	while (ti->nr_free_requests) {
		ti->nr_free_requests--;
		tmp = ti->free_requests.next;
		req = list_entry(tmp, tux_req_t, free);
		list_del(tmp);
		DEBUG_DEL_LIST(tmp);
		DEC_STAT(nr_free_pending);
		__kfree_req(req, ti);
		count++;
	}
	spin_unlock_irqrestore(&ti->free_requests_lock,flags);

	return count;
}

static tux_req_t * kmalloc_req (threadinfo_t * ti)
{
	struct list_head *tmp;
	unsigned long flags;
	tux_req_t *req;

	spin_lock_irqsave(&ti->free_requests_lock, flags);
	if (ti->nr_free_requests) {
		ti->nr_free_requests--;
		tmp = ti->free_requests.next;
		req = list_entry(tmp, tux_req_t, free);
		list_del(tmp);
		DEBUG_DEL_LIST(tmp);
		DEC_STAT(nr_free_pending);
		req->magic = TUX_MAGIC;
		spin_unlock_irqrestore(&ti->free_requests_lock, flags);
	} else {
		spin_unlock_irqrestore(&ti->free_requests_lock, flags);
		req = tux_kmalloc(sizeof(*req));
		ti->nr_requests++;
		memset (req, 0, sizeof(*req));
		list_add(&req->all, &ti->all_requests);
	}
	req->magic = TUX_MAGIC;
	INC_STAT(nr_allocated);
	init_waitqueue_entry(&req->sleep, current);
	init_waitqueue_entry(&req->ftp_sleep, current);
	INIT_LIST_HEAD(&req->work);
	INIT_LIST_HEAD(&req->free);
	INIT_LIST_HEAD(&req->lru);
	req->ti = ti;
	req->total_bytes = 0;
	SET_TIMESTAMP(req->accept_timestamp);
	req->first_timestamp = jiffies;
	req->fd = -1;
	init_timer(&req->keepalive_timer);
	init_timer(&req->output_timer);

	Dprintk("allocated NEW req %p.\n", req);
	return req;
}

void kfree_req (tux_req_t *req)
{
	threadinfo_t * ti = req->ti;
	unsigned long flags;

	Dprintk("freeing req %p.\n", req);

	if (req->magic != TUX_MAGIC)
		TUX_BUG();
	spin_lock_irqsave(&ti->free_requests_lock,flags);
	req->magic = 0;
	DEC_STAT(nr_allocated);
	if (req->sock || req->dentry || req->private)
		TUX_BUG();
	if (ti->nr_free_requests > tux_max_free_requests)
		__kfree_req(req, ti);
	else {
		req->error = 0;
		ti->nr_free_requests++;

		// the free requests queue is LIFO
		list_add(&req->free, &ti->free_requests);
		INC_STAT(nr_free_pending);
	}
	spin_unlock_irqrestore(&ti->free_requests_lock,flags);
}

static void __add_req_to_workqueue (tux_req_t *req)
{
	threadinfo_t *ti = req->ti;

	if (!list_empty(&req->work))
		TUX_BUG();
	Dprintk("work-queueing request %p at %p/%p.\n", req, __builtin_return_address(0), __builtin_return_address(1));
	if (connection_too_fast(req))
		list_add_tail(&req->work, &ti->work_pending);
	else
		list_add(&req->work, &ti->work_pending);
	INC_STAT(nr_work_pending);
	wake_up_process(ti->thread);
	return;
}

void add_req_to_workqueue (tux_req_t *req)
{
	unsigned long flags;
	threadinfo_t *ti = req->ti;

	spin_lock_irqsave(&ti->work_lock, flags);
	__add_req_to_workqueue(req);
	spin_unlock_irqrestore(&ti->work_lock, flags);
}

void del_output_timer (tux_req_t *req)
{
#if CONFIG_SMP
	if (!spin_is_locked(&req->ti->work_lock))
		TUX_BUG();
#endif
	if (!list_empty(&req->lru)) {
		list_del(&req->lru);
		DEBUG_DEL_LIST(&req->lru);
		req->ti->nr_lru--;
	}
	Dprintk("del output timeout for req %p.\n", req);
	del_timer(&req->output_timer);
}

static void output_timeout_fn (unsigned long data);

#define OUTPUT_TIMEOUT HZ

static void add_output_timer (tux_req_t *req)
{
	struct timer_list *timer = &req->output_timer;

	timer->data = (unsigned long) req;
	timer->function = &output_timeout_fn;
	mod_timer(timer, jiffies + OUTPUT_TIMEOUT);
}

static void output_timeout_fn (unsigned long data)
{
	tux_req_t *req = (tux_req_t *)data;

	if (connection_too_fast(req)) {
		add_output_timer(req);
//		mod_timer(&req->output_timer, jiffies + OUTPUT_TIMEOUT);
		return;
	}
	output_space_event(req);
}

void output_timeout (tux_req_t *req)
{
	Dprintk("output timeout for req %p.\n", req);
	if (test_and_set_bit(0, &req->wait_output_space))
		TUX_BUG();
	INC_STAT(nr_output_space_pending);
	add_output_timer(req);
}

void __del_keepalive_timer (tux_req_t *req)
{
#if CONFIG_SMP
	if (!spin_is_locked(&req->ti->work_lock))
		TUX_BUG();
#endif
	if (!list_empty(&req->lru)) {
		list_del(&req->lru);
		DEBUG_DEL_LIST(&req->lru);
		req->ti->nr_lru--;
	}
	Dprintk("del keepalive timeout for req %p.\n", req);
	del_timer(&req->keepalive_timer);
}

static void keepalive_timeout_fn (unsigned long data)
{
	tux_req_t *req = (tux_req_t *)data;

#if CONFIG_TUX_DEBUG
	Dprintk("req %p timed out after %d sec!\n", req, tux_keepalive_timeout);
	if (tux_Dprintk)
		print_req(req);
#endif
	Dprintk("req->error = TUX_ERROR_CONN_TIMEOUT!\n");
	req->error = TUX_ERROR_CONN_TIMEOUT;
	if (!idle_event(req))
		output_space_event(req);
}

void __add_keepalive_timer (tux_req_t *req)
{
	struct timer_list *timer = &req->keepalive_timer;

	if (!tux_keepalive_timeout)
		TUX_BUG();
#if CONFIG_SMP
	if (!spin_is_locked(&req->ti->work_lock))
		TUX_BUG();
#endif

	if (!list_empty(&req->lru))
		TUX_BUG();
	if (req->ti->nr_lru > tux_max_keepalives) {
		struct list_head *head, *last;
		tux_req_t *last_req;

		head = &req->ti->lru;
		last = head->prev;
		if (last == head)
			TUX_BUG();
		last_req = list_entry(last, tux_req_t, lru);
		list_del(last);
		DEBUG_DEL_LIST(last);
		req->ti->nr_lru--;

		Dprintk("LRU-aging req %p!\n", last_req);
		last_req->error = TUX_ERROR_CONN_TIMEOUT;
		if (!__idle_event(last_req))
			__output_space_event(last_req);
	}
	list_add(&req->lru, &req->ti->lru);
	req->ti->nr_lru++;

	timer->expires = jiffies + tux_keepalive_timeout * HZ;
	timer->data = (unsigned long) req;
	timer->function = &keepalive_timeout_fn;
	add_timer(timer);
}

static int __output_space_event (tux_req_t *req)
{
	if (!req || (req->magic != TUX_MAGIC))
		TUX_BUG();

	if (!test_and_clear_bit(0, &req->wait_output_space)) {
		Dprintk("output space ready event at <%p>, on non-idle %p.\n", __builtin_return_address(0), req);
		return 0;
	}

	Dprintk("output space ready event at <%p>, %p was waiting!\n", __builtin_return_address(0), req);
	DEC_STAT(nr_output_space_pending);

	del_keepalive_timer(req);
	del_output_timer(req);

	__add_req_to_workqueue(req);
	return 1;
}

int output_space_event (tux_req_t *req)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&req->ti->work_lock, flags);
	ret = __output_space_event(req);
	spin_unlock_irqrestore(&req->ti->work_lock, flags);

	return ret;
}

static int __idle_event (tux_req_t *req)
{
	threadinfo_t *ti;

	if (!req || (req->magic != TUX_MAGIC))
		TUX_BUG();
	ti = req->ti;

	if (!test_and_clear_bit(0, &req->idle_input)) {
		Dprintk("data ready event at <%p>, on non-idle %p.\n", __builtin_return_address(0), req);
		return 0;
	}

	Dprintk("data ready event at <%p>, %p was idle!\n", __builtin_return_address(0), req);
	del_keepalive_timer(req);
	del_output_timer(req);
	DEC_STAT(nr_idle_input_pending);

	req->sock->sk->tp_pinfo.af_tcp.ack.pingpong = tux_ack_pingpong;
	SET_TIMESTAMP(req->accept_timestamp);

	__add_req_to_workqueue(req);

	return 1;
}

int idle_event (tux_req_t *req)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&req->ti->work_lock, flags);
	ret = __idle_event(req);
	spin_unlock_irqrestore(&req->ti->work_lock, flags);

	return ret;
}

#define HANDLE_CALLBACK_1(callback, tux_name, real_name, param...)	\
	tux_req_t *req;					\
							\
	read_lock(&sk->callback_lock);			\
	req = sk->user_data;				\
							\
	Dprintk("callback "#callback"(%p) req %p.\n",	\
		sk->callback, req);			\
							\
	if (!req) {					\
		if (sk->callback == tux_name) {		\
			printk("BUG: "#callback" "#tux_name" "#real_name" no req!"); \
			TUX_BUG();			\
		}					\
		read_unlock(&sk->callback_lock);	\
		if (sk->callback)			\
			sk->callback(param);		\
		return;					\
	}						\

#define HANDLE_CALLBACK_2(callback, tux_name, real_name, param...)	\
	Dprintk(#tux_name"() on %p.\n", req);		\
	if (req->magic != TUX_MAGIC)			\
		TUX_BUG();				\
	if (req->real_name)				\
		req->real_name(param);

#define HANDLE_CALLBACK(callback, tux_name, real_name, param...)	\
	HANDLE_CALLBACK_1(callback,tux_name,real_name,param)	\
	HANDLE_CALLBACK_2(callback,tux_name,real_name,param)

static void tux_data_ready (struct sock *sk, int len)
{
	HANDLE_CALLBACK_1(data_ready, tux_data_ready, real_data_ready, sk, len);

	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_write_space (struct sock *sk)
{
	HANDLE_CALLBACK(write_space, tux_write_space, real_write_space, sk);

	Dprintk("sk->wmem_queued: %d, sk->sndbuf: %d.\n",
		sk->wmem_queued, sk->sndbuf);

	if (tcp_wspace(sk) >= tcp_min_write_space(sk)) {
		clear_bit(SOCK_NOSPACE, &sk->socket->flags);
		if (!idle_event(req))
			output_space_event(req);
	}
	read_unlock(&sk->callback_lock);
}

static void tux_error_report (struct sock *sk)
{
	HANDLE_CALLBACK(error_report, tux_error_report, real_error_report, sk);

	req->error = TUX_ERROR_CONN_CLOSE;
	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_state_change (struct sock *sk)
{
	HANDLE_CALLBACK(state_change, tux_state_change, real_state_change, sk);

	if (req->sock && req->sock->sk &&
				(req->sock->sk->state > TCP_ESTABLISHED)) {
		Dprintk("req %p changed to TCP non-established!\n", req);
		Dprintk("req->sock: %p\n", req->sock);
		if (req->sock)
			Dprintk("req->sock->sk: %p\n", req->sock->sk);
		if (req->sock && req->sock->sk)
			Dprintk("TCP state: %d\n", req->sock->sk->state);
		Dprintk("req->error = TUX_ERROR_CONN_CLOSE!\n");
		req->error = TUX_ERROR_CONN_CLOSE;
	}
	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_destruct (struct sock *sk)
{
	BUG();
}

static void tux_ftp_data_ready (struct sock *sk, int len)
{
	HANDLE_CALLBACK_1(data_ready, tux_ftp_data_ready,
				ftp_real_data_ready, sk, len);
	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_ftp_write_space (struct sock *sk)
{
	HANDLE_CALLBACK_1(write_space, tux_ftp_write_space,
				ftp_real_write_space, sk);

	Dprintk("sk->wmem_queued: %d, sk->sndbuf: %d.\n",
		sk->wmem_queued, sk->sndbuf);

	if (tcp_wspace(sk) >= sk->sndbuf/10*8) {
		clear_bit(SOCK_NOSPACE, &sk->socket->flags);
		if (!idle_event(req))
			output_space_event(req);
	}
	read_unlock(&sk->callback_lock);
}

static void tux_ftp_error_report (struct sock *sk)
{
	HANDLE_CALLBACK(error_report, tux_ftp_error_report,
		ftp_real_error_report, sk);

	TDprintk("req %p sock %p got TCP errors on FTP data connection!\n", req, sk);
	TDprintk("req->error = TUX_ERROR_CONN_CLOSE!\n");
	req->error = TUX_ERROR_CONN_CLOSE;
	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_ftp_state_change (struct sock *sk)
{
	HANDLE_CALLBACK(state_change, tux_ftp_state_change,
			ftp_real_state_change, sk);

	if (req->sock && req->sock->sk &&
			(req->sock->sk->state > TCP_ESTABLISHED)) {
		Dprintk("req %p FTP control sock changed to TCP non-established!\n", req);
		Dprintk("req->sock: %p\n", req->sock);
		TDprintk("req->error = TUX_ERROR_CONN_CLOSE!\n");

		req->error = TUX_ERROR_CONN_CLOSE;
	}
	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_ftp_create_child (struct sock *sk, struct sock *newsk)
{
	HANDLE_CALLBACK(create_child, tux_ftp_create_child,
			ftp_real_create_child, sk, newsk);

	newsk->user_data = NULL;
	newsk->data_ready = req->ftp_real_data_ready;
	newsk->state_change = req->ftp_real_state_change;
	newsk->write_space = req->ftp_real_write_space;
	newsk->error_report = req->ftp_real_error_report;
	newsk->create_child = req->ftp_real_create_child;
	newsk->destruct = req->ftp_real_destruct;

	if (!idle_event(req))
		output_space_event(req);
	read_unlock(&sk->callback_lock);
}

static void tux_ftp_destruct (struct sock *sk)
{
	BUG();
}

static void link_tux_socket (tux_req_t *req, struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (req->sock)
		TUX_BUG();
	if (sk->destruct == tux_destruct)
		TUX_BUG();
	/*
	 * (No need to lock the socket, we just want to
	 * make sure that events from now on go through
	 * tux_data_ready())
	 */
	write_lock_irq(&sk->callback_lock);

	req->sock = sock;
	sk->user_data = req;

	req->real_data_ready = sk->data_ready;
	req->real_state_change = sk->state_change;
	req->real_write_space = sk->write_space;
	req->real_error_report = sk->error_report;
	req->real_destruct = sk->destruct;

	sk->data_ready = tux_data_ready;
	sk->state_change = tux_state_change;
	sk->write_space = tux_write_space;
	sk->error_report = tux_error_report;
	sk->destruct = tux_destruct;

	write_unlock_irq(&sk->callback_lock);

	if (req->real_destruct == tux_destruct)
		TUX_BUG();
	req->client_addr = sk->daddr;
	req->client_port = sk->dport;

	add_wait_queue(sk->sleep, &req->sleep);
}

void __link_data_socket (tux_req_t *req, struct socket *sock,
						struct sock *sk)
{
	/*
	 * (No need to lock the socket, we just want to
	 * make sure that events from now on go through
	 * tux_data_ready())
	 */
	write_lock_irq(&sk->callback_lock);

	req->data_sock = sock;
	sk->user_data = req;

	req->ftp_real_data_ready = sk->data_ready;
	req->ftp_real_state_change = sk->state_change;
	req->ftp_real_write_space = sk->write_space;
	req->ftp_real_error_report = sk->error_report;
	req->ftp_real_create_child = sk->create_child;
	req->ftp_real_destruct = sk->destruct;

	sk->data_ready = tux_ftp_data_ready;
	sk->state_change = tux_ftp_state_change;
	sk->write_space = tux_ftp_write_space;
	sk->error_report = tux_ftp_error_report;
	sk->create_child = tux_ftp_create_child;
	sk->destruct = tux_ftp_destruct;

	if (req->ftp_real_destruct == tux_ftp_destruct)
		TUX_BUG();

	write_unlock_irq(&sk->callback_lock);

	add_wait_queue(sk->sleep, &req->ftp_sleep);
}

void link_tux_data_socket (tux_req_t *req, struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (req->data_sock)
		TUX_BUG();
	if (sk->destruct == tux_ftp_destruct)
		TUX_BUG();
	__link_data_socket(req, sock, sk);
}

void unlink_tux_socket (tux_req_t *req)
{
	struct sock *sk;
	
	if (!req->sock || !req->sock->sk)
		return;
	sk = req->sock->sk;

	write_lock_irq(&sk->callback_lock);
	if (!sk->user_data)
		TUX_BUG();
	if (req->real_destruct == tux_destruct)
		TUX_BUG();

	sk->user_data = NULL;

	sk->data_ready = req->real_data_ready;
	sk->state_change = req->real_state_change;
	sk->write_space = req->real_write_space;
	sk->error_report = req->real_error_report;
	sk->destruct = req->real_destruct;

	if (sk->destruct == tux_destruct)
		TUX_BUG();

	req->real_data_ready = NULL;
	req->real_state_change = NULL;
	req->real_write_space = NULL;
	req->real_error_report = NULL;
	req->real_destruct = NULL;

	write_unlock_irq(&sk->callback_lock);

	remove_wait_queue(sk->sleep, &req->sleep);
}

void unlink_tux_data_socket (tux_req_t *req)
{
	struct sock *sk;
	
	if (!req->data_sock || !req->data_sock->sk)
		return;
	sk = req->data_sock->sk;

	write_lock_irq(&sk->callback_lock);

	if (req->real_destruct == tux_ftp_destruct)
		TUX_BUG();

	sk->user_data = NULL;
	sk->data_ready = req->ftp_real_data_ready;
	sk->state_change = req->ftp_real_state_change;
	sk->write_space = req->ftp_real_write_space;
	sk->error_report = req->ftp_real_error_report;
	sk->create_child = req->ftp_real_create_child;
	sk->destruct = req->ftp_real_destruct;

	req->ftp_real_data_ready = NULL;
	req->ftp_real_state_change = NULL;
	req->ftp_real_write_space = NULL;
	req->ftp_real_error_report = NULL;
	req->ftp_real_create_child = NULL;
	req->ftp_real_destruct = NULL;

	write_unlock_irq(&sk->callback_lock);

	if (sk->destruct == tux_ftp_destruct)
		TUX_BUG();

	remove_wait_queue(sk->sleep, &req->ftp_sleep);
}

void add_tux_atom (tux_req_t *req, atom_func_t *atom)
{
	Dprintk("adding TUX atom %p to req %p, atom_idx: %d, at %p/%p.\n",
		atom, req, req->atom_idx, __builtin_return_address(0), __builtin_return_address(1));
	if (req->atom_idx == MAX_TUX_ATOMS)
		TUX_BUG();
	req->atoms[req->atom_idx] = atom;
	req->atom_idx++;
}

void del_tux_atom (tux_req_t *req)
{
	if (!req->atom_idx)
		TUX_BUG();
	req->atom_idx--;
	Dprintk("removing TUX atom %p to req %p, atom_idx: %d, at %p.\n",
		req->atoms[req->atom_idx], req, req->atom_idx, __builtin_return_address(0));
}

void tux_schedule_atom (tux_req_t *req, int cachemiss)
{
	if (!list_empty(&req->work))
		TUX_BUG();
	if (!req->atom_idx)
		TUX_BUG();
	req->atom_idx--;
	Dprintk("DOING TUX atom %p, req %p, atom_idx: %d, at %p.\n",
		req->atoms[req->atom_idx], req, req->atom_idx, __builtin_return_address(0));
	req->atoms[req->atom_idx](req, cachemiss);
	Dprintk("DONE TUX atom %p, req %p, atom_idx: %d, at %p.\n",
		req->atoms[req->atom_idx], req, req->atom_idx, __builtin_return_address(0));
}

/*
 * Puts newly accepted connections into the inputqueue. This is the
 * first step in the life of a TUX request.
 */
int accept_requests (threadinfo_t *ti)
{
	int count = 0, last_count = 0, error, socknr = 0;
	struct socket *sock, *new_sock;
	struct tcp_opt *tp1, *tp2;
	tux_req_t *req;

	if (ti->nr_requests > tux_max_connect)
		goto out;

repeat:
	for (socknr = 0; socknr < CONFIG_TUX_NUMSOCKETS; socknr++) {
		tux_listen_t *tux_listen;

		tux_listen = ti->listen + socknr;
		sock = tux_listen->sock;
		if (!sock)
			break;
		if (current->need_resched)
			break;

	tp1 = &sock->sk->tp_pinfo.af_tcp;
	/*
	 * Quick test to see if there are connections on the queue.
	 * This is cheaper than accept() itself because this saves us
	 * the allocation of a new socket. (Which doesn't seem to be
	 * used anyway)
	 */
	if (tp1->accept_queue) {
		tux_proto_t *proto;

		if (!count++)
			__set_task_state(current, TASK_RUNNING);

		new_sock = sock_alloc();
		if (!new_sock)
			goto out;

		new_sock->type = sock->type;
		new_sock->ops = sock->ops;

		error = sock->ops->accept(sock, new_sock, O_NONBLOCK);
		if (error < 0)
			goto err;
		if (new_sock->sk->state != TCP_ESTABLISHED)
			goto err;

		tp2 = &new_sock->sk->tp_pinfo.af_tcp;
		tp2->nonagle = 2;
		tp2->ack.pingpong = tux_ack_pingpong;
		new_sock->sk->reuse = 1;
		new_sock->sk->urginline = 1;

		/* Allocate a request-entry for the connection */
		req = kmalloc_req(ti);
		if (!req)
			BUG();
		link_tux_socket(req, new_sock);

		proto = req->proto = tux_listen->proto;

		proto->got_request(req);
	}
	}
	if (count != last_count) {
		last_count = count;
		goto repeat;
	}
out:
	return count;
err:
	sock_release(new_sock);
	goto out;
}

