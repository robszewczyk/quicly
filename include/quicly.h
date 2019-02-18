/*
 * Copyright (c) 2017 Fastly, Kazuho Oku
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef quicly_h
#define quicly_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "picotls.h"
#include "quicly/constants.h"
#include "quicly/frame.h"
#include "quicly/linklist.h"
#include "quicly/loss.h"
#include "quicly/recvstate.h"
#include "quicly/sendstate.h"
#include "quicly/maxsender.h"

#ifndef QUICLY_DEBUG
#define QUICLY_DEBUG 0
#endif

/* invariants! */
#define QUICLY_LONG_HEADER_BIT 0x80
#define QUICLY_PACKET_IS_LONG_HEADER(first_byte) (((first_byte)&QUICLY_LONG_HEADER_BIT) != 0)

#define QUICLY_PROTOCOL_VERSION 0xff000011

#define QUICLY_MAX_CID_LEN 18
#define QUICLY_STATELESS_RESET_TOKEN_LEN 16

typedef struct st_quicly_datagram_t {
    ptls_iovec_t data;
    socklen_t salen;
    struct sockaddr sa;
} quicly_datagram_t;

/**
 * Event types used for logging.
 *
 * CONNECT, ACCEPT, SEND, RECEIVE are major events that correspond to the external functions of quicly (e.g. quicly_connect).
 * Timestamp, CID, first-octet, etc. are included as attributes.
 *
 * The rest are minor (i.e. in-detail) events. They are categorized by prefix (e.g., "PACKET", "CC"). They do not contain timestamp
 * or CID. The time and the connection can be determined by the major event that precedes the minor event.
 */
typedef enum en_quicly_event_type_t {
    QUICLY_EVENT_TYPE_CONNECT,
    QUICLY_EVENT_TYPE_ACCEPT,
    QUICLY_EVENT_TYPE_SEND,
    QUICLY_EVENT_TYPE_RECEIVE,
    QUICLY_EVENT_TYPE_FREE,
    QUICLY_EVENT_TYPE_PACKET_PREPARE,
    QUICLY_EVENT_TYPE_PACKET_COMMIT,
    QUICLY_EVENT_TYPE_PACKET_ACKED,
    QUICLY_EVENT_TYPE_PACKET_LOST,
    QUICLY_EVENT_TYPE_CRYPTO_DECRYPT,
    QUICLY_EVENT_TYPE_CRYPTO_HANDSHAKE,
    QUICLY_EVENT_TYPE_CRYPTO_UPDATE_SECRET,
    QUICLY_EVENT_TYPE_CC_TLP,
    QUICLY_EVENT_TYPE_CC_RTO,
    QUICLY_EVENT_TYPE_CC_ACK_RECEIVED,
    QUICLY_EVENT_TYPE_CC_CONGESTION,
    QUICLY_EVENT_TYPE_STREAM_SEND,
    QUICLY_EVENT_TYPE_STREAM_RECEIVE,
    QUICLY_EVENT_TYPE_STREAM_ACKED,
    QUICLY_EVENT_TYPE_STREAM_LOST,
    QUICLY_EVENT_TYPE_QUIC_VERSION_SWITCH,
    QUICLY_EVENT_TYPE_CLOSE_SEND,
    QUICLY_EVENT_TYPE_CLOSE_RECEIVE,
    QUICLY_EVENT_TYPE_QUICTRACE_SEND,
    QUICLY_EVENT_TYPE_QUICTRACE_RECV,
    QUICLY_EVENT_TYPE_QUICTRACE_LOST,
    QUICLY_EVENT_TYPE_QUICTRACE_SEND_STREAM,
    QUICLY_EVENT_TYPE_QUICTRACE_RECV_STREAM,
    QUICLY_EVENT_TYPE_QUICTRACE_RECV_ACK,
} quicly_event_type_t;

/**
 * an array of event names corresponding to quicly_event_type_t
 */
extern const char *quicly_event_type_names[];

typedef enum en_quicly_event_attribute_type_t {
    QUICLY_EVENT_ATTRIBUTE_NULL,
    QUICLY_EVENT_ATTRIBUTE_TYPE_INT_MIN,
    QUICLY_EVENT_ATTRIBUTE_TIME = QUICLY_EVENT_ATTRIBUTE_TYPE_INT_MIN,
    QUICLY_EVENT_ATTRIBUTE_EPOCH,
    QUICLY_EVENT_ATTRIBUTE_PACKET_TYPE,
    QUICLY_EVENT_ATTRIBUTE_PACKET_NUMBER,
    QUICLY_EVENT_ATTRIBUTE_PACKET_SIZE,
    QUICLY_EVENT_ATTRIBUTE_CONNECTION,
    QUICLY_EVENT_ATTRIBUTE_TLS_ERROR,
    QUICLY_EVENT_ATTRIBUTE_OFFSET,
    QUICLY_EVENT_ATTRIBUTE_LENGTH,
    QUICLY_EVENT_ATTRIBUTE_STREAM_ID,
    QUICLY_EVENT_ATTRIBUTE_FIN,
    QUICLY_EVENT_ATTRIBUTE_IS_ENC,
    QUICLY_EVENT_ATTRIBUTE_ENC_LEVEL,
    QUICLY_EVENT_ATTRIBUTE_QUIC_VERSION,
    QUICLY_EVENT_ATTRIBUTE_ACK_ONLY,
    QUICLY_EVENT_ATTRIBUTE_MAX_LOST_PN,
    QUICLY_EVENT_ATTRIBUTE_END_OF_RECOVERY,
    QUICLY_EVENT_ATTRIBUTE_BYTES_IN_FLIGHT,
    QUICLY_EVENT_ATTRIBUTE_CWND,
    QUICLY_EVENT_ATTRIBUTE_NEWLY_ACKED,
    QUICLY_EVENT_ATTRIBUTE_FIRST_OCTET,
    QUICLY_EVENT_ATTRIBUTE_CC_TYPE,
    QUICLY_EVENT_ATTRIBUTE_CC_END_OF_RECOVERY,
    QUICLY_EVENT_ATTRIBUTE_CC_EXIT_RECOVERY,
    QUICLY_EVENT_ATTRIBUTE_ACKED_PACKETS,
    QUICLY_EVENT_ATTRIBUTE_ACKED_BYTES,
    QUICLY_EVENT_ATTRIBUTE_STATE,
    QUICLY_EVENT_ATTRIBUTE_ERROR_CODE,
    QUICLY_EVENT_ATTRIBUTE_FRAME_TYPE,
    QUICLY_EVENT_ATTRIBUTE_ACK_BLOCK_BEGIN,
    QUICLY_EVENT_ATTRIBUTE_ACK_BLOCK_END,
    QUICLY_EVENT_ATTRIBUTE_ACK_DELAY,
    QUICLY_EVENT_ATTRIBUTE_TYPE_INT_MAX,
    QUICLY_EVENT_ATTRIBUTE_TYPE_VEC_MIN = QUICLY_EVENT_ATTRIBUTE_TYPE_INT_MAX,
    QUICLY_EVENT_ATTRIBUTE_DCID = QUICLY_EVENT_ATTRIBUTE_TYPE_VEC_MIN,
    QUICLY_EVENT_ATTRIBUTE_SCID,
    QUICLY_EVENT_ATTRIBUTE_REASON_PHRASE,
    QUICLY_EVENT_ATTRIBUTE_TYPE_VEC_MAX
} quicly_event_attribute_type_t;

/**
 * an array of attribute names corresponding to quicly_event_attribute_type_t
 */
extern const char *quicly_event_attribute_names[];

typedef struct st_quicly_event_attribute_t {
    quicly_event_attribute_type_t type;
    union {
        ptls_iovec_t v;
        int64_t i;
    } value;
} quicly_event_attribute_t;

typedef struct st_quicly_cid_t quicly_cid_t;
typedef struct st_quicly_cid_plaintext_t quicly_cid_plaintext_t;
typedef struct st_quicly_context_t quicly_context_t;
typedef struct st_quicly_conn_t quicly_conn_t;
typedef struct st_quicly_stream_t quicly_stream_t;

#define QUICLY_CALLBACK_TYPE0(ret, name)                                                                                           \
    typedef struct st_quicly_##name##_cb {                                                                                         \
        ret (*cb)(struct st_quicly_##name##_cb * self);                                                                            \
    } quicly_##name##_cb

#define QUICLY_CALLBACK_TYPE(ret, name, ...)                                                                                       \
    typedef struct st_quicly_##name##_cb {                                                                                         \
        ret (*cb)(struct st_quicly_##name##_cb * self, __VA_ARGS__);                                                               \
    } quicly_##name##_cb

/**
 * allocates a packet buffer
 */
QUICLY_CALLBACK_TYPE(quicly_datagram_t *, alloc_packet, socklen_t salen, size_t payloadsize);
/**
 * frees a packet buffer
 */
QUICLY_CALLBACK_TYPE(void, free_packet, quicly_datagram_t *packet);
/**
 * encrypts CID
 */
QUICLY_CALLBACK_TYPE(void, encrypt_cid, quicly_cid_t *encrypted, const quicly_cid_plaintext_t *plaintext);
/**
 * decrypts CID. plaintext->thread_id should contain a randomly distributed number when validation fails, so that the value can be
 * used for distributing load among the threads within the process.
 * @param len length of encrypted bytes if known, or 0 if unknown (short header packet)
 * @return length of the CID, or SIZE_MAX if decryption failed
 */
QUICLY_CALLBACK_TYPE(size_t, decrypt_cid, quicly_cid_plaintext_t *plaintext, const void *encrypted, size_t len);
/**
 * called when stream is being open. Application is expected to create it's corresponding state and tie it to stream->data.
 */
QUICLY_CALLBACK_TYPE(int, stream_open, quicly_stream_t *stream);
/**
 * called when the connection is closed by peer
 */
QUICLY_CALLBACK_TYPE(void, closed_by_peer, quicly_conn_t *conn, int err, uint64_t frame_type, const char *reason,
                     size_t reason_len);
/**
 * returns current time in milliseconds
 */
QUICLY_CALLBACK_TYPE0(int64_t, now);
/**
 * for event logging
 */
QUICLY_CALLBACK_TYPE(void, event_log, quicly_event_type_t type, const quicly_event_attribute_t *attributes, size_t num_attributes);

typedef struct st_quicly_max_stream_data_t {
    uint64_t bidi_local, bidi_remote, uni;
} quicly_max_stream_data_t;

/**
 * Transport Parameters; the struct contains "configuration parameters", ODCID is managed separately
 */
typedef struct st_quicly_transport_parameters_t {
    /**
     * in octets
     */
    quicly_max_stream_data_t max_stream_data;
    /**
     * in octets
     */
    uint64_t max_data;
    /**
     * in seconds
     */
    uint64_t idle_timeout;
    /**
     *
     */
    uint64_t max_streams_bidi;
    /**
     *
     */
    uint64_t max_streams_uni;
    /**
     * quicly ignores the value set for quicly_context_t::transport_parameters
     */
    uint8_t ack_delay_exponent;
    /**
     * in milliseconds; quicly ignores the value set for quicly_context_t::transport_parameters
     */
    uint8_t max_ack_delay;
} quicly_transport_parameters_t;

struct st_quicly_cid_t {
    uint8_t cid[QUICLY_MAX_CID_LEN];
    uint8_t len;
};

/**
 * Guard value. We would never send path_id of this value.
 */
#define QUICLY_MAX_PATH_ID UINT8_MAX

/**
 * The structure of CID issued by quicly.
 *
 * Authentication of the CID can be done by validating if server_id and thread_id contain correct values.
 */
struct st_quicly_cid_plaintext_t {
    /**
     * the internal "connection ID" unique to each connection (rather than QUIC's CID being unique to each path)
     */
    uint32_t master_id;
    /**
     * path ID of the connection; we issue up to 255 CIDs per connection (see QUICLY_MAX_PATH_ID)
     */
    uint32_t path_id : 8;
    /**
     * for intra-node routing
     */
    uint32_t thread_id : 24;
    /**
     * for inter-node routing; available only when using a 16-byte cipher to encrypt CIDs, otherwise set to zero. See
     * quicly_context_t::is_clustered.
     */
    uint64_t node_id;
};

struct st_quicly_context_t {
    /**
     * tls context to use
     */
    ptls_context_t *tls;
    /**
     * MTU
     */
    uint16_t max_packet_size;
    /**
     * loss detection parameters
     */
    quicly_loss_conf_t *loss;
    /**
     * transport parameters
     */
    quicly_transport_parameters_t transport_params;
    /**
     * client-only
     */
    unsigned enforce_version_negotiation : 1;
    /**
     * if inter-node routing is used (by utilising quicly_cid_plaintext_t::node_id)
     */
    unsigned is_clustered : 1;
    /**
     * callback for allocating memory for raw packet
     */
    quicly_alloc_packet_cb *alloc_packet;
    /**
     * callback for freeing memory allocated by alloc_packet
     */
    quicly_free_packet_cb *free_packet;
    /**
     *
     */
    quicly_encrypt_cid_cb *encrypt_cid;
    /**
     *
     */
    quicly_decrypt_cid_cb *decrypt_cid;
    /**
     * callback called when a new stream is opened by peer
     */
    quicly_stream_open_cb *stream_open;
    /**
     * callback called when a connection is closed by peer
     */
    quicly_closed_by_peer_cb *closed_by_peer;
    /**
     * returns current time in milliseconds
     */
    quicly_now_cb *now;
    /**
     * optional callback for debug logging
     */
    struct {
        /**
         * Bitmask of event types to be logged. The field is a union of (1 << event_type).
         */
        uint64_t mask;
        /**
         * The callback. The value MUST be non-NULL when mask is set to non-zero. quicly_default_event_log is a functor provided by
         * by quicly that logs the events in JSON streaming format.
         */
        quicly_event_log_cb *cb;
    } event_log;
};

/**
 * connection state
 */
typedef enum {
    /**
     * before observing the first message from peer
     */
    QUICLY_STATE_FIRSTFLIGHT,
    /**
     * while connected
     */
    QUICLY_STATE_CONNECTED,
    /**
     * sending close, but haven't seen the peer sending close
     */
    QUICLY_STATE_CLOSING,
    /**
     * we do not send CLOSE (at the moment), enter draining mode when receiving CLOSE
     */
    QUICLY_STATE_DRAINING
} quicly_state_t;

struct st_quicly_conn_streamgroup_state_t {
    uint32_t num_streams;
    quicly_stream_id_t next_stream_id;
};

struct _st_quicly_conn_public_t {
    quicly_context_t *ctx;
    quicly_state_t state;
    /**
     * identifier assigned by the application. `path_id` stores the next value to be issued
     */
    quicly_cid_plaintext_t master_id;
    struct {
        /**
         * the SCID used in long header packets
         */
        quicly_cid_t src_cid;
        /**
         * TODO clear this at some point (probably when the server releases all the keys below epoch=3)
         */
        quicly_cid_t offered_cid;
        struct st_quicly_conn_streamgroup_state_t bidi, uni;
    } host;
    struct {
        quicly_cid_t cid;
        struct st_quicly_conn_streamgroup_state_t bidi, uni;
        struct sockaddr *sa;
        socklen_t salen;
        quicly_transport_parameters_t transport_params;
    } peer;
    struct {
        uint64_t received, sent, lost, ack_received;
    } num_packets;
    uint64_t num_bytes_sent;
    uint32_t version;
    void *data;
};

typedef enum {
    /**
     * initial state
     */
    QUICLY_SENDER_STATE_NONE,
    /**
     * to be sent. Changes to UNACKED when sent out by quicly_send
     */
    QUICLY_SENDER_STATE_SEND,
    /**
     * inflight. changes to SEND (when packet is deemed lost), or ACKED (when packet is ACKed)
     */
    QUICLY_SENDER_STATE_UNACKED,
    /**
     * the sent value acknowledged by peer
     */
    QUICLY_SENDER_STATE_ACKED,
} quicly_sender_state_t;

/**
 * API that allows applications to specify it's own send / receive buffer.  The callback should be assigned by the
 * `quicly_context_t::on_stream_open` callback.
 */
typedef struct st_quicly_stream_callbacks_t {
    /**
     * called when the stream is destroyed
     */
    void (*on_destroy)(quicly_stream_t *stream);
    /**
     * called whenever data can be retired from the send buffer, specifying the amount that can be newly removed
     */
    void (*on_send_shift)(quicly_stream_t *stream, size_t delta);
    /**
     * asks the application to fill the frame payload.  `off` is the offset within the buffer (the beginning position of the buffer
     * changes as `on_send_shift` is invoked). `len` is an in/out argument that specifies the size of the buffer / amount of data
     * being written.  `wrote_all` is a boolean out parameter indicating if the application has written all the available data.  See
     * also quicly_stream_sync_sendbuf.
     */
    int (*on_send_emit)(quicly_stream_t *stream, size_t off, void *dst, size_t *len, int *wrote_all);
    /**
     * called when a STOP_SENDING frame is received.  Do not call `quicly_reset_stream` in response.  The stream will be
     * automatically reset by quicly.
     */
    int (*on_send_stop)(quicly_stream_t *stream, int err);
    /**
     * called when data is newly received.  `off` is the offset within the buffer (the beginning position changes as the application
     * calls `quicly_stream_sync_recvbuf`.  Applications should consult `quicly_stream_t::recvstate` to see if it has contiguous
     * input.
     */
    int (*on_receive)(quicly_stream_t *stream, size_t off, const void *src, size_t len);
    /**
     * called when a RESET_STREAM frame is received
     */
    int (*on_receive_reset)(quicly_stream_t *stream, int err);
} quicly_stream_callbacks_t;

struct st_quicly_stream_t {
    /**
     *
     */
    quicly_conn_t *conn;
    /**
     * stream id
     */
    quicly_stream_id_t stream_id;
    /**
     *
     */
    const quicly_stream_callbacks_t *callbacks;
    /**
     * send buffer
     */
    quicly_sendstate_t sendstate;
    /**
     * receive buffer
     */
    quicly_recvstate_t recvstate;
    /**
     *
     */
    void *data;
    /**
     *
     */
    unsigned streams_blocked : 1;
    /**
     *
     */
    struct {
        /**
         * send window
         */
        uint64_t max_stream_data;
        /**
         * 1 + maximum offset of data that has been sent at least once (NOT counting eos)
         */
        uint64_t max_sent;
        /**
         *
         */
        struct {
            quicly_sender_state_t sender_state;
            uint16_t error_code;
        } stop_sending;
        /**
         * rst_stream
         */
        struct {
            /**
             * STATE_NONE until RST is generated
             */
            quicly_sender_state_t sender_state;
            uint16_t error_code;
        } rst;
        /**
         * sends receive window updates to peer
         */
        quicly_maxsender_t max_stream_data_sender;
        /**
         * linklist of pending streams
         */
        struct {
            quicly_linklist_t control; /* links to conn_t::control (or to conn_t::streams_blocked if the blocked flag is set) */
            quicly_linklist_t stream;
        } pending_link;
    } _send_aux;
    /**
     *
     */
    struct {
        /**
         * size of the receive window
         */
        uint32_t window;
    } _recv_aux;
};

typedef struct st_quicly_decoded_packet_t {
    /**
     * octets of the entire packet
     */
    ptls_iovec_t octets;
    struct {
        /**
         * destination CID
         */
        struct {
            /**
             * CID visible on wire
             */
            ptls_iovec_t encrypted;
            /**
             * the decrypted CID; note that the value is not authenticated
             */
            quicly_cid_plaintext_t plaintext;
        } dest;
        /**
         * source CID; {NULL, 0} if is a short header packet
         */
        ptls_iovec_t src;
    } cid;
    /**
     * version; 0 if is a short header packet
     */
    uint32_t version;
    /**
     * token if available; otherwise {NULL, 0}
     */
    ptls_iovec_t token;
    /**
     * starting offset of data (i.e., version-dependent area of a long header packet (version numbers in case of VN), odcid (in case
     * of retry), or encrypted PN)
     */
    size_t encrypted_off;
    /**
     * size of the datagram
     */
    size_t datagram_size;
} quicly_decoded_packet_t;

extern const quicly_context_t quicly_default_context;

/**
 *
 */
size_t quicly_decode_packet(quicly_context_t *ctx, quicly_decoded_packet_t *packet, const uint8_t *src, size_t len);
/**
 *
 */
uint64_t quicly_determine_packet_number(uint32_t bits, uint32_t mask, uint64_t next_expected);
/**
 *
 */
static int quicly_cid_is_equal(const quicly_cid_t *cid, ptls_iovec_t vec);
/**
 *
 */
static quicly_context_t *quicly_get_context(quicly_conn_t *conn);
/**
 *
 */
static const quicly_cid_plaintext_t *quicly_get_master_id(quicly_conn_t *conn);
/**
 *
 */
static const quicly_cid_t *quicly_get_offered_cid(quicly_conn_t *conn);
/**
 *
 */
static const quicly_cid_t *quicly_get_peer_cid(quicly_conn_t *conn);
/**
 *
 */
static const quicly_transport_parameters_t *quicly_get_peer_transport_parameters(quicly_conn_t *conn);
/**
 *
 */
static quicly_state_t quicly_get_state(quicly_conn_t *conn);
/**
 *
 */
int quicly_connection_is_ready(quicly_conn_t *conn);
/**
 *
 */
static uint32_t quicly_num_streams(quicly_conn_t *conn);
/**
 *
 */
static int quicly_is_client(quicly_conn_t *conn);
/**
 *
 */
static quicly_stream_id_t quicly_get_next_stream_id(quicly_conn_t *conn, int uni);
/**
 *
 */
static void quicly_get_peername(quicly_conn_t *conn, struct sockaddr **sa, socklen_t *salen);
/**
 *
 */
static void quicly_get_packet_stats(quicly_conn_t *conn, uint64_t *num_received, uint64_t *num_sent, uint64_t *num_lost,
                                    uint64_t *num_ack_received, uint64_t *num_bytes_sent);
/**
 *
 */
void quicly_get_max_data(quicly_conn_t *conn, uint64_t *send_permitted, uint64_t *sent, uint64_t *consumed);
/**
 *
 */
static void **quicly_get_data(quicly_conn_t *conn);
/**
 * destroys a connection object.
 */
void quicly_free(quicly_conn_t *conn);
/**
 * closes the connection.  `err` is the application error code using the coalesced scheme (see QUICLY_ERROR_* macros), or zero (no
 * error; indicating idle close).  An application should continue calling quicly_recieve and quicly_send, until they return
 * QUICLY_ERROR_FREE_CONNECTION.  At this point, it is should call quicly_free.
 */
int quicly_close(quicly_conn_t *conn, int err, const char *reason_phrase);
/**
 *
 */
int64_t quicly_get_first_timeout(quicly_conn_t *conn);
/**
 *
 */
quicly_datagram_t *quicly_send_version_negotiation(quicly_context_t *ctx, struct sockaddr *sa, socklen_t salen,
                                                   ptls_iovec_t dest_cid, ptls_iovec_t src_cid);
/**
 *
 */
quicly_datagram_t *quicly_send_retry(quicly_context_t *ctx, struct sockaddr *sa, socklen_t salen, ptls_iovec_t dcid,
                                     ptls_iovec_t scid, ptls_iovec_t odcid, ptls_iovec_t token);
/**
 *
 */
int quicly_send(quicly_conn_t *conn, quicly_datagram_t **packets, size_t *num_packets);
/**
 *
 */
int quicly_receive(quicly_conn_t *conn, quicly_decoded_packet_t *packet);
/**
 *
 */
int quicly_is_destination(quicly_conn_t *conn, struct sockaddr *sa, socklen_t salen, quicly_decoded_packet_t *decoded);
/**
 *
 */
int quicly_encode_transport_parameter_list(const quicly_transport_parameters_t *params, const quicly_cid_t *odcid, int is_client,
                                           ptls_buffer_t *buf);
/**
 *
 */
int quicly_decode_transport_parameter_list(quicly_transport_parameters_t *params, quicly_cid_t *odcid, int is_client,
                                           const uint8_t *src, const uint8_t *end);
/**
 * Initiates a new connection.
 * @param new_cid the CID to be used for the connection. path_id is ignored.
 */
int quicly_connect(quicly_conn_t **conn, quicly_context_t *ctx, const char *server_name, struct sockaddr *sa, socklen_t salen,
                   const quicly_cid_plaintext_t *new_cid, ptls_handshake_properties_t *handshake_properties,
                   const quicly_transport_parameters_t *resumed_transport_params);
/**
 * accepts a new connection
 * @param new_cid the CID to be used for the connection. When an error is being returned, the application can reuse the CID provided
 *                to the function.
 */
int quicly_accept(quicly_conn_t **conn, quicly_context_t *ctx, struct sockaddr *sa, socklen_t salen,
                  quicly_decoded_packet_t *packet, ptls_iovec_t retry_odcid, const quicly_cid_plaintext_t *new_cid,
                  ptls_handshake_properties_t *handshake_properties);
/**
 *
 */
quicly_stream_t *quicly_get_stream(quicly_conn_t *conn, quicly_stream_id_t stream_id);
/**
 *
 */
int quicly_open_stream(quicly_conn_t *conn, quicly_stream_t **stream, int unidirectional);
/**
 *
 */
void quicly_reset_stream(quicly_stream_t *stream, int err);
/**
 *
 */
void quicly_request_stop(quicly_stream_t *stream, int err);
/**
 *
 */
int quicly_stream_sync_sendbuf(quicly_stream_t *stream, int activate);
/**
 *
 */
void quicly_stream_sync_recvbuf(quicly_stream_t *stream, size_t shift_amount);
/**
 *
 */
static int quicly_stream_is_client_initiated(quicly_stream_id_t stream_id);
/**
 *
 */
static int quicly_stream_is_unidirectional(quicly_stream_id_t stream_id);
/**
 *
 */
static int quicly_stream_is_self_initiated(quicly_stream_t *stream);
/**
 *
 */
extern quicly_alloc_packet_cb quicly_default_alloc_packet_cb;
/**
 *
 */
extern quicly_free_packet_cb quicly_default_free_packet_cb;
/**
 *
 */
quicly_encrypt_cid_cb *quicly_new_default_encrypt_cid_cb(ptls_cipher_algorithm_t *algo, const void *key);
/**
 *
 */
void quicly_free_default_encrypt_cid_cb(quicly_encrypt_cid_cb *self);
/**
 *
 */
quicly_decrypt_cid_cb *quicly_new_default_decrypt_cid_cb(ptls_cipher_algorithm_t *algo, const void *key);
/**
 *
 */
void quicly_free_default_decrypt_cid_cb(quicly_decrypt_cid_cb *self);
/**
 *
 */
extern quicly_now_cb quicly_default_now_cb;
/**
 *
 */
quicly_event_log_cb *quicly_new_default_event_log_cb(FILE *fp);
/**
 *
 */
void quicly_free_default_event_log_cb(quicly_event_log_cb *self);
/**
 *
 */
void quicly_amend_ptls_context(ptls_context_t *ptls);
/**
 *
 */
char *quicly_hexdump(const uint8_t *bytes, size_t len, size_t indent);

/* inline definitions */

inline quicly_state_t quicly_get_state(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return c->state;
}

inline uint32_t quicly_num_streams(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return c->host.bidi.num_streams + c->host.uni.num_streams + c->peer.bidi.num_streams + c->peer.uni.num_streams;
}

inline int quicly_cid_is_equal(const quicly_cid_t *cid, ptls_iovec_t vec)
{
    return cid->len == vec.len && memcmp(cid->cid, vec.base, vec.len) == 0;
}

inline quicly_context_t *quicly_get_context(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return c->ctx;
}

inline const quicly_cid_plaintext_t *quicly_get_master_id(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return &c->master_id;
}

inline const quicly_cid_t *quicly_get_offered_cid(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return &c->host.offered_cid;
}

inline const quicly_cid_t *quicly_get_peer_cid(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return &c->peer.cid;
}

inline const quicly_transport_parameters_t *quicly_get_peer_transport_parameters(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return &c->peer.transport_params;
}

inline int quicly_is_client(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return (c->host.bidi.next_stream_id & 1) == 0;
}

inline quicly_stream_id_t quicly_get_next_stream_id(quicly_conn_t *conn, int uni)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return uni ? c->host.uni.next_stream_id : c->host.bidi.next_stream_id;
}

inline void quicly_get_peername(quicly_conn_t *conn, struct sockaddr **sa, socklen_t *salen)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    *sa = c->peer.sa;
    *salen = c->peer.salen;
}

inline void **quicly_get_data(quicly_conn_t *conn)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    return &c->data;
}

inline int quicly_stream_is_client_initiated(quicly_stream_id_t stream_id)
{
    if (stream_id < 0)
        return (stream_id & 1) != 0;
    return (stream_id & 1) == 0;
}

inline int quicly_stream_is_unidirectional(quicly_stream_id_t stream_id)
{
    if (stream_id < 0)
        return 0;
    return (stream_id & 2) != 0;
}

inline int quicly_stream_is_self_initiated(quicly_stream_t *stream)
{
    return quicly_stream_is_client_initiated(stream->stream_id) == quicly_is_client(stream->conn);
}

inline void quicly_get_packet_stats(quicly_conn_t *conn, uint64_t *num_received, uint64_t *num_sent, uint64_t *num_lost,
                                    uint64_t *num_ack_received, uint64_t *num_bytes_sent)
{
    struct _st_quicly_conn_public_t *c = (struct _st_quicly_conn_public_t *)conn;
    *num_received = c->num_packets.received;
    *num_sent = c->num_packets.sent;
    *num_lost = c->num_packets.lost;
    *num_ack_received = c->num_packets.ack_received;
    *num_bytes_sent = c->num_bytes_sent;
}

#ifdef __cplusplus
}
#endif

#endif
