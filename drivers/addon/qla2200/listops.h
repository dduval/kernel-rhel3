/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

/* Management functions for various lists */

/*
 * Found in kernel 2.4.9 and higher in include/linux/lists.h
 *
 * Iterate over a list safe against removal of list.
 *
 */
#if !defined(list_for_each_safe)
#define list_for_each_safe(pos, n, head) \
	for( pos= (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next )
#endif


#if !defined(list_for_each_entry)
#define list_for_each_entry(pos, head, member)                          \
	for (pos = list_entry((head)->next, typeof(*pos), member);      \
	    &pos->member != (head);                                    \
	    pos = list_entry(pos->member.next, typeof(*pos), member))

#endif
#if !defined(list_for_each_entry_safe)
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
	    n = list_entry(pos->member.next, typeof(*pos), member);	\
	    &pos->member != (head);					\
	    pos = n, n = list_entry(n->member.next, typeof(*n), member))
#endif

/* Non-standard definitions. */
static inline void __qla_list_splice(struct list_head *list,
    struct list_head *head)
{
	struct list_head	*first = list->next;
	struct	list_head	*last = list->prev;
	struct	list_head	*at = head->next;

	first->prev = head;
	head->next = first;

	last->next = at;
	at->prev = last;

}

static inline void qla_list_splice_init(struct list_head *list,
    struct list_head *head)
{
	if (!list_empty(list)) {
		__qla_list_splice(list, head);
		INIT_LIST_HEAD(list);
	}
}

/* __add_to_done_queue()
 * 
 * Place SRB command on done queue.
 *
 * Input:
 *      ha           = host pointer
 *      sp           = srb pointer.
 * Locking:
 * 	this function assumes the ha->list_lock is already taken
 */
static inline void 
__add_to_done_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if (sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        /* Place block on done queue */
        sp->cmd->host_scribble = (unsigned char *) NULL;
        sp->state = SRB_DONE_STATE;
        list_add_tail(&sp->list,&ha->done_queue);
        ha->done_q_cnt++;
	sp->ha = ha;
}

/* __add_to_free_queue()
 * 
 * Place SRB command on free queue.
 *
 * Input:
 *      ha           = host pointer
 *      sp           = srb pointer.
 * Locking:
 * 	this function assumes the ha->list_lock is already taken
 */
static inline void 
__add_to_free_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if (atomic_read(&sp->ref_count) != 0)
                BUG();
	*/


        /* Place block on free queue */
        sp->state = SRB_FREE_STATE;
        list_add_tail(&sp->list,&ha->free_queue);
        ha->srb_cnt++;
}

static inline void 
__add_to_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        /* Place block on retry queue */
        list_add_tail(&sp->list,&ha->retry_queue);
        ha->retry_q_cnt++;
        sp->flags |= SRB_WATCHDOG;
        sp->state = SRB_RETRY_STATE;
	sp->ha = ha;
}

static inline void 
__add_to_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        /* Place block on retry queue */
        list_add_tail(&sp->list,&ha->scsi_retry_queue);
        ha->scsi_retry_q_cnt++;
        sp->state = SRB_SCSI_RETRY_STATE;
	sp->ha = ha;
}

static inline void 
add_to_done_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_done_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
add_to_free_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_free_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
add_to_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_retry_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
add_to_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);
        __add_to_scsi_retry_queue(ha,sp);
        spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*
 * __del_from_retry_queue
 *      Function used to remove a command block from the
 *      watchdog timer queue.
 *
 *      Note: Must insure that command is on watchdog
 *            list before calling del_from_retry_queue
 *            if (sp->flags & SRB_WATCHDOG)
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function assumes the list_lock is already taken
 */
static inline void 
__del_from_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        list_del_init(&sp->list);

        sp->flags &= ~(SRB_WATCHDOG | SRB_BUSY);
        sp->state = SRB_NO_QUEUE_STATE;
        ha->retry_q_cnt--;
}

/*
 * __del_from_scsi_retry_queue
 *      Function used to remove a command block from the
 *      scsi retry queue.
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function assumes the list_lock is already taken
 */
static inline void 
__del_from_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        list_del_init(&sp->list);

        ha->scsi_retry_q_cnt--;
        sp->state = SRB_NO_QUEUE_STATE;
}

/*
 * del_from_retry_queue
 *      Function used to remove a command block from the
 *      watchdog timer queue.
 *
 *      Note: Must insure that command is on watchdog
 *            list before calling del_from_retry_queue
 *            if (sp->flags & SRB_WATCHDOG)
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function takes and releases the list_lock
 */
static inline void 
del_from_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        /*	if (unlikely(!(sp->flags & SRB_WATCHDOG)))
        		BUG();*/
        spin_lock_irqsave(&ha->list_lock, flags);

        /*	if (unlikely(list_empty(&ha->retry_queue)))
        		BUG();*/

        __del_from_retry_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}
/*
 * del_from_scsi_retry_queue
 *      Function used to remove a command block from the
 *      scsi retry queue.
 *
 * Input: 
 *      ha = adapter block pointer.
 *      sp = srb pointer.
 * Locking:
 *	this function takes and releases the list_lock
 */
static inline void 
del_from_scsi_retry_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        /*	if (unlikely(list_empty(&ha->scsi_retry_queue)))
        		BUG();*/

        __del_from_scsi_retry_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
__del_from_free_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        ha->srb_cnt--;
        list_del_init(&sp->list);
        sp->state = SRB_NO_QUEUE_STATE;
}

/*
 * __add_to_pending_queue
 *      Add the standard SCB job to the bottom of standard SCB commands.
 *
 * Input:
 * COMPLETE!!!
 *      q  = SCSI LU pointer.
 *      sp = srb pointer.
 *      SCSI_LU_Q lock must be already obtained.
 */
static inline void 
__add_to_pending_queue(struct scsi_qla_host *ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE &&
        	sp->state != SRB_FREE_STATE &&
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

	list_add_tail(&sp->list, &ha->pending_queue);
	ha->qthreads++;
	sp->state = SRB_PENDING_STATE;
}

static inline void 
__add_to_pending_queue_head(struct scsi_qla_host *ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_FREE_STATE &&
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

	list_add(&sp->list, &ha->pending_queue);
	ha->qthreads++;
	sp->state = SRB_PENDING_STATE;
}

/* returns 1 if the queue was empty before the add */
static inline int
add_to_pending_queue(struct scsi_qla_host *ha, srb_t *sp)
{
	unsigned long flags;
	int ret;
	
	spin_lock_irqsave(&ha->list_lock, flags);
	ret = list_empty(&ha->pending_queue);	
	__add_to_pending_queue(ha, sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
	return ret;
}

static inline void
add_to_pending_queue_head(struct scsi_qla_host *ha, srb_t *sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_pending_queue_head(ha, sp);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
__del_from_pending_queue(struct scsi_qla_host *ha, srb_t *sp)
{
	list_del_init(&sp->list);
	ha->qthreads--;
	sp->state = SRB_NO_QUEUE_STATE;
}

/*
 * Failover Stuff.
 */
static inline void
__add_to_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
        if( sp->state != SRB_NO_QUEUE_STATE && 
        	sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

        list_add_tail(&sp->list,&ha->failover_queue);
        ha->failover_cnt++;
        sp->state = SRB_FAILOVER_STATE;
	sp->ha = ha;
}

static inline void add_to_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        __add_to_failover_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}
static inline void __del_from_failover_queue(struct scsi_qla_host * ha, srb_t *
                sp)
{
        ha->failover_cnt--;
        list_del_init(&sp->list);
        sp->state = SRB_NO_QUEUE_STATE;
}

static inline void del_from_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        __del_from_failover_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void 
del_from_pending_queue(struct scsi_qla_host * ha, srb_t * sp)
{
        unsigned long flags;

        spin_lock_irqsave(&ha->list_lock, flags);

        __del_from_pending_queue(ha,sp);

        spin_unlock_irqrestore(&ha->list_lock, flags);
}
 
