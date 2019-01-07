#ifndef _WS_CLIENT_H_
#define _WS_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS_CLIENT_ENABLE_WSS        CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS

typedef struct ws_client* ws_client_handle_t;

/* header without masking-key(4 bytes) */
typedef struct ws_header
{
    uint8_t opcode : 4;
    uint8_t srv_3 : 1;
    uint8_t srv_2 : 1;
    uint8_t srv_1 : 1;
    uint8_t fin : 1;
    uint8_t payload_len : 7;
    uint8_t mask : 1;
    uint8_t ext_payload_len[8];
} ws_header_t;

typedef enum {
    WS_EVENT_ERROR = 0,
    WS_EVENT_CONNECTED,          /*!< connected event, additional context: session_present flag */
    WS_EVENT_DISCONNECTED,       /*!< disconnected event */
    WS_EVENT_DATA,               /*!< data event */
    WS_EVENT_DATA_FIN,           /*!< data event, for fragment, use out_buffer together */
} ws_event_id_t;

/**
 * WS event configuration structure
 */
typedef struct {
    ws_event_id_t event_id;       /*!< WS event type */
    ws_client_handle_t client;    /*!< WS client handle for this event */
    void *user_context;           /*!< User context passed from WS client config */
    ws_header_t *ws_header;       /*!< WS header */
    uint8_t *data;                /*!< Data asociated with this event */
    int data_len;                 /*!< Lenght of the data for this event */
} ws_event_t;

typedef esp_err_t (* ws_event_callback_t)(ws_event_t *event);

typedef struct {
    ws_event_callback_t event_handle;       /*!< handle for WS events */
    const char *uri;                        /*!< Complete WS URI */
    bool disable_auto_reconnect;            /*!< this ws client will reconnect to server (when errors/disconnect). Set disable_auto_reconnect=true to disable */
    void *user_context;                     /*!< pass user context to this option, then can receive that context in ``event->user_context`` */
    int task_prio;                          /*!< WS task priority, default is 5, can be changed in ``make menuconfig`` */
    int task_stack;                         /*!< WS task stack size, default is 6144 bytes, can be changed in ``make menuconfig`` */
    int buffer_size;                        /*!< size of WS send/receive buffer*/
    uint8_t *out_buffer;
    int out_buffer_size;
    const char *cert_pem;                   /*!< Pointer to certificate data in PEM format for server verify (with SSL), default is NULL, not required to verify the server */
    const char *client_cert_pem;            /*!< Pointer to certificate data in PEM format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_key_pem` has to be provided. */
    const char *client_key_pem;             /*!< Pointer to private key data in PEM format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_cert_pem` has to be provided. */
} ws_client_config_t;

ws_client_handle_t ws_client_init(const ws_client_config_t *config);
esp_err_t ws_client_start(ws_client_handle_t client);
esp_err_t ws_client_stop(ws_client_handle_t client);
esp_err_t ws_client_destroy(ws_client_handle_t client);

esp_err_t ws_client_write_data(ws_client_handle_t client, char *buff, int len);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
