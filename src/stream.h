#ifndef STREAM_H
#define STREAM_H

#include "rax.h"
#include "listpack.h"

/* Stream item ID: a 128 bit number composed of a milliseconds time and
 * a sequence counter. IDs generated in the same millisecond (or in a past
 * millisecond if the clock jumped backward) will use the millisecond time
 * of the latest generated ID and an incremented sequence. */
typedef struct streamID {
    uint64_t ms;        /* Unix time in milliseconds. */
    uint64_t seq;       /* Sequence number. */
} streamID;

typedef struct stream {
    rax *rax;               /* The radix tree holding the stream. */    //这是当前消息组的生产者消息树 key为streamID消息id val为listpack消息
    uint64_t length;        /* Number of elements inside this stream. */    //当前消息组消息个数
    streamID last_id;       /* Zero if there are yet no items. */   //当前消息组stream最后插入的消息id
    rax *cgroups;           /* Consumer groups dictionary: name -> streamCG */  //这是当前消息组的消费者群组树 key为群组名称 val为streamCG streamCG主要记录消费者未确认消息pel 该群组所有消费者consumers last_id是当前消费到哪里的消息id
} stream;

/* We define an iterator to iterate stream items in an abstract way, without
 * caring about the radix tree + listpack representation. Technically speaking
 * the iterator is only used inside streamReplyWithRange(), so could just
 * be implemented inside the function, but practically there is the AOF
 * rewriting code that also needs to iterate the stream to emit the XADD
 * commands. */
typedef struct streamIterator {
    stream *stream;         /* The stream we are iterating. */
    streamID master_id;     /* ID of the master entry at listpack head. */
    uint64_t master_fields_count;       /* Master entries # of fields. */
    unsigned char *master_fields_start; /* Master entries start in listpack. */
    unsigned char *master_fields_ptr;   /* Master field to emit next. */
    int entry_flags;                    /* Flags of entry we are emitting. */
    int rev;                /* True if iterating end to start (reverse). */
    uint64_t start_key[2];  /* Start key as 128 bit big endian. */
    uint64_t end_key[2];    /* End key as 128 bit big endian. */
    raxIterator ri;         /* Rax iterator. */
    unsigned char *lp;      /* Current listpack. */
    unsigned char *lp_ele;  /* Current listpack cursor. */
    unsigned char *lp_flags; /* Current entry flags pointer. */
    /* Buffers used to hold the string of lpGet() when the element is
     * integer encoded, so that there is no string representation of the
     * element inside the listpack itself. */
    unsigned char field_buf[LP_INTBUF_SIZE];
    unsigned char value_buf[LP_INTBUF_SIZE];
} streamIterator;

/* Consumer group. */
typedef struct streamCG {
    streamID last_id;       /* Last delivered (not acknowledged) ID for this
                               group. Consumers that will just ask for more
                               messages will served with IDs > than this. */
    rax *pel;               /* Pending entries list. This is a radix tree that      //未确认消息 key是消息id val是streamNACK
                               has every message delivered to consumers (without
                               the NOACK option) that was yet not acknowledged
                               as processed. The key of the radix tree is the
                               ID as a 64 bit big endian number, while the
                               associated value is a streamNACK structure.*/
    rax *consumers;         /* A radix tree representing the consumers by name
                               and their associated representation in the form
                               of streamConsumer structures. */
} streamCG;

/* A specific consumer in a consumer group.  */
typedef struct streamConsumer {
    mstime_t seen_time;         /* Last time this consumer was active. */
    sds name;                   /* Consumer name. This is how the consumer
                                   will be identified in the consumer group
                                   protocol. Case sensitive. */
    rax *pel;                   /* Consumer specific pending entries list: all
                                   the pending messages delivered to this
                                   consumer not yet acknowledged. Keys are
                                   big endian message IDs, while values are
                                   the same streamNACK structure referenced
                                   in the "pel" of the conumser group structure
                                   itself, so the value is shared. */
} streamConsumer;

/* Pending (yet not acknowledged) message in a consumer group. */
typedef struct streamNACK {
    mstime_t delivery_time;     /* Last time this message was delivered. */
    uint64_t delivery_count;    /* Number of times this message was delivered.*/
    streamConsumer *consumer;   /* The consumer this message was delivered to
                                   in the last delivery. */
} streamNACK;

/* Stream propagation informations, passed to functions in order to propagate
 * XCLAIM commands to AOF and slaves. */
typedef struct sreamPropInfo {
    robj *keyname;
    robj *groupname;
} streamPropInfo;

/* Prototypes of exported APIs. */
struct client;

stream *streamNew(void);
void freeStream(stream *s);
size_t streamReplyWithRange(client *c, stream *s, streamID *start, streamID *end, size_t count, int rev, streamCG *group, streamConsumer *consumer, int flags, streamPropInfo *spi);
void streamIteratorStart(streamIterator *si, stream *s, streamID *start, streamID *end, int rev);
int streamIteratorGetID(streamIterator *si, streamID *id, int64_t *numfields);
void streamIteratorGetField(streamIterator *si, unsigned char **fieldptr, unsigned char **valueptr, int64_t *fieldlen, int64_t *valuelen);
void streamIteratorStop(streamIterator *si);
streamCG *streamLookupCG(stream *s, sds groupname);
streamConsumer *streamLookupConsumer(streamCG *cg, sds name, int create);
streamCG *streamCreateCG(stream *s, char *name, size_t namelen, streamID *id);
streamNACK *streamCreateNACK(streamConsumer *consumer);
void streamDecodeID(void *buf, streamID *id);
int streamCompareID(streamID *a, streamID *b);

#endif
