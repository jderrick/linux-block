#ifndef CFQ_IOSCHED_H
#define CFQ_IOSCHED_H

struct request_queue;
struct bio;
struct request;

int cfq_merge(struct request_queue *, struct request **, struct bio *);
void cfq_merged_request(struct request_queue *, struct request *, int);
void cfq_merged_requests(struct request_queue *, struct request *,
			 struct request *);
int cfq_allow_merge(struct request_queue *, struct request *, struct bio *);
int cfq_dispatch_requests(struct request_queue *, int);
void cfq_insert_request(struct request_queue *, struct request *);
void cfq_activate_request(struct request_queue *, struct request *);
void cfq_deactivate_request(struct request_queue *, struct request *);
int cfq_queue_empty(struct request_queue *);
void cfq_completed_request(struct request_queue *, struct request *);
int cfq_set_request(struct request_queue *, struct request *, gfp_t);
void cfq_put_request(struct request *);
int cfq_may_queue(struct request_queue *, int);

#endif
