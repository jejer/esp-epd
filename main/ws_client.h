#ifndef _WS_CLIENT_H_
#define _WS_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WS_CLIENT_ENABLE_WSS        CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS
#define WS_TASK_PRIORITY            5
#define WS_TASK_STACK               (4*1024)
#define WS_NETWORK_TIMEOUT_MS       (10*1000)
#define WS_RECONNECT_TIMEOUT_MS     (10*1000)
#define WS_BUFFER_SIZE_BYTE         5*1024
#define WS_DEFAULT_PORT             80
#define WSS_DEFAULT_PORT            443

typedef struct ws_client* ws_client_handle_t;

/**
 * @brief WS event types.
 *
 * User event handler receives context data in `ws_event_t` structure with
 *  - `user_context` - user data from `ws_client_config_t`
 *  - `client` - ws client handle
 *  - various other data depending on event type
 *
 */
typedef enum {
    WS_EVENT_ERROR = 0,
    WS_EVENT_CONNECTED,          /*!< connected event, additional context: session_present flag */
    WS_EVENT_DISCONNECTED,       /*!< disconnected event */
    WS_EVENT_DATA,               /*!< data event, additional context */
} ws_event_id_t;

typedef enum {
    WS_TRANSPORT_UNKNOWN = 0x0,
    WS_TRANSPORT_OVER_TCP,       /*!< WS over TCP, using scheme: ``ws`` */
    WS_TRANSPORT_OVER_SSL        /*!< WS over SSL, using scheme: ``wss`` */
} ws_transport_t;

/**
 * WS event configuration structure
 */
typedef struct {
    ws_event_id_t event_id;       /*!< WS event type */
    ws_client_handle_t client;    /*!< WS client handle for this event */
    void *user_context;                 /*!< User context passed from WS client config */
    uint8_t *data;                         /*!< Data asociated with this event */
    int data_len;                       /*!< Lenght of the data for this event */
} ws_event_t;

typedef esp_err_t (* ws_event_callback_t)(ws_event_t *event);

/**
 * WS client configuration structure
 */
typedef struct {
    ws_event_callback_t event_handle;       /*!< handle for WS events */
    const char *host;                       /*!< WS server domain (ipv4 as string) */
    const char *uri;                        /*!< Complete WS broker URI */
    uint32_t port;                          /*!< WS server port */
    bool disable_auto_reconnect;            /*!< this ws client will reconnect to server (when errors/disconnect). Set disable_auto_reconnect=true to disable */
    void *user_context;                     /*!< pass user context to this option, then can receive that context in ``event->user_context`` */
    int task_prio;                          /*!< WS task priority, default is 5, can be changed in ``make menuconfig`` */
    int task_stack;                         /*!< WS task stack size, default is 6144 bytes, can be changed in ``make menuconfig`` */
    int buffer_size;                        /*!< size of WS send/receive buffer, default is 1024 */
    const char *cert_pem;                   /*!< Pointer to certificate data in PEM format for server verify (with SSL), default is NULL, not required to verify the server */
    const char *client_cert_pem;            /*!< Pointer to certificate data in PEM format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_key_pem` has to be provided. */
    const char *client_key_pem;             /*!< Pointer to private key data in PEM format for SSL mutual authentication, default is NULL, not required if mutual authentication is not needed. If it is not NULL, also `client_cert_pem` has to be provided. */
    ws_transport_t transport;               /*!< overrides URI transport */
} ws_client_config_t;

ws_client_handle_t ws_client_init(const ws_client_config_t *config);
esp_err_t ws_client_set_uri(ws_client_handle_t client, const char *uri);
esp_err_t ws_client_start(ws_client_handle_t client);
esp_err_t ws_client_stop(ws_client_handle_t client);
esp_err_t ws_client_destroy(ws_client_handle_t client);

esp_err_t ws_client_write_data(ws_client_handle_t client, const char *buff, int len);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif
