#ifndef ELV_INTERN_H
#define ELV_INTERN_H

#include <linux/blkdev.h>
#include <linux/elevator.h>

#include "cfq-iosched.h"

static inline int elv_call_allow_merge_fn(struct request_queue *q,
					  struct request *rq, struct bio *bio)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_allow_merge_fn == cfq_allow_merge)
		return cfq_allow_merge(q, rq, bio);
#endif
	return q->elv_ops.elevator_allow_merge_fn(q, rq, bio);
}

static inline void elv_call_activate_req_fn(struct request_queue *q,
					    struct request *rq)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_activate_req_fn == cfq_activate_request)
		cfq_activate_request(q, rq);
	else
#endif
		q->elv_ops.elevator_activate_req_fn(q, rq);
}

static inline void elv_call_deactivate_req_fn(struct request_queue *q,
					      struct request *rq)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_deactivate_req_fn == cfq_deactivate_request)
		cfq_deactivate_request(q, rq);
	else
#endif
	q->elv_ops.elevator_deactivate_req_fn(q, rq);
}

static inline int elv_call_merge_fn(struct request_queue *q,
				    struct request **rq, struct bio *bio)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_merge_fn == cfq_merge)
		return cfq_merge(q, rq, bio);
#endif
	return q->elv_ops.elevator_merge_fn(q, rq, bio);
}

static inline void elv_call_merged_fn(struct request_queue *q,
				      struct request *rq, int type)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_merged_fn == cfq_merged_request)
		cfq_merged_request(q, rq, type);
	else
#endif
		q->elv_ops.elevator_merged_fn(q, rq, type);
}

static inline void elv_call_merge_req_fn(struct request_queue *q,
					 struct request *rq,
					 struct request *next)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_merge_req_fn == cfq_merged_requests)
		cfq_merged_requests(q, rq, next);
	else
#endif
		q->elv_ops.elevator_merge_req_fn(q, rq, next);
}

static inline int elv_call_dispatch_fn(struct request_queue *q, int force)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_dispatch_fn == cfq_dispatch_requests)
		return cfq_dispatch_requests(q, force);
#endif
	return q->elv_ops.elevator_dispatch_fn(q, force);

}

static inline void elv_call_add_req_fn(struct request_queue *q,
				       struct request *rq)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_add_req_fn == cfq_insert_request)
		cfq_insert_request(q, rq);
	else
#endif
		q->elv_ops.elevator_add_req_fn(q, rq);
}

static inline int elv_call_queue_empty_fn(struct request_queue *q)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_queue_empty_fn == cfq_queue_empty)
		return cfq_queue_empty(q);
#endif
	return q->elv_ops.elevator_queue_empty_fn(q);
}

static inline struct request *
elv_call_former_req_fn(struct request_queue *q, struct request *rq)
{
	if (q->elv_ops.elevator_former_req_fn == elv_rb_former_request)
		return elv_rb_former_request(q, rq);

	return q->elv_ops.elevator_former_req_fn(q, rq);
}

static inline struct request *
elv_call_latter_req_fn(struct request_queue *q, struct request *rq)
{
	if (q->elv_ops.elevator_latter_req_fn == elv_rb_latter_request)
		return elv_rb_latter_request(q, rq);

	return q->elv_ops.elevator_latter_req_fn(q, rq);
}

static inline int
elv_call_set_req_fn(struct request_queue *q, struct request *rq, gfp_t gfp_mask)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_set_req_fn == cfq_set_request)
		return cfq_set_request(q, rq, gfp_mask);
#endif
	return q->elv_ops.elevator_set_req_fn(q, rq, gfp_mask);
}

static inline void
elv_call_put_req_fn(struct request_queue *q, struct request *rq)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_put_req_fn == cfq_put_request)
		cfq_put_request(rq);
	else
#endif
		q->elv_ops.elevator_put_req_fn(rq);
}

static inline int elv_call_may_queue_fn(struct request_queue *q, int rw)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_may_queue_fn == cfq_may_queue)
		return cfq_may_queue(q, rw);
#endif
	return q->elv_ops.elevator_may_queue_fn(q, rw);
}

static inline void
elv_call_completed_req_fn(struct request_queue *q, struct request *rq)
{
#if defined(CONFIG_IOSCHED_CFQ_BUILTIN)
	if (q->elv_ops.elevator_completed_req_fn == cfq_completed_request)
		cfq_completed_request(q, rq);
	else
#endif
		q->elv_ops.elevator_completed_req_fn(q, rq);
}

#endif
