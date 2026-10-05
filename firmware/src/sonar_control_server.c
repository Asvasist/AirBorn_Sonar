#include "sonar_control_server.h"
#include "sonar_control_protocol.h"
#include "sonar_experiment.h"
#include "sonar_frames.h"
#include "sonar_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/errno.h"
#include "xil_printf.h"
#include <string.h>

typedef struct {
    uint32_t token, session, opcode, bytes;
    TickType_t submitted;
    uint8_t data[SONAR_CONTROL_MAX_PAYLOAD];
} request_t;
typedef struct { uint32_t token, session, words[SONAR_CONTROL_REPLY_WORDS]; } response_t;
static QueueHandle_t requests, responses;
static bool started;
#define RPC_MS 2000U
#define SOCKET_MS 5000U

bool sonar_control_create(void)
{
    requests=xQueueCreate(2,sizeof(request_t)); responses=xQueueCreate(2,sizeof(response_t));
    return requests!=NULL && responses!=NULL;
}
void sonar_control_poll(bool mutable)
{
    request_t q;
    if (requests!=NULL && xQueueReceive(requests,&q,0)==pdPASS) {
        response_t r={.token=q.token,.session=q.session};
        if (q.opcode!=CTRL_END_SESSION && (TickType_t)(xTaskGetTickCount()-q.submitted)>=pdMS_TO_TICKS(RPC_MS)) {
            sonar_experiment_reply(CTRL_TIMEOUT,r.words);
        } else { sonar_experiment_command(q.session,q.opcode,q.data,q.bytes,mutable,r.words); }
        /* A timed-out caller may have left one obsolete reply. Discard it if
         * necessary; token matching prevents it satisfying a newer request. */
        if (xQueueSend(responses,&r,0)!=pdPASS) {
            response_t discarded;
            (void)xQueueReceive(responses,&discarded,0);
            (void)xQueueSend(responses,&r,0);
        }
    }
}
static bool rpc(request_t *q, response_t *r)
{
    q->submitted=xTaskGetTickCount();
    if (xQueueSend(requests,q,pdMS_TO_TICKS(RPC_MS))!=pdPASS) { return false; }
    while ((TickType_t)(xTaskGetTickCount()-q->submitted)<pdMS_TO_TICKS(RPC_MS)) {
        if (xQueueReceive(responses,r,pdMS_TO_TICKS(20))==pdPASS && r->token==q->token && r->session==q->session) { return true; }
    }
    return false;
}
static bool io(int fd, uint8_t *data, uint32_t count, bool sending)
{
    TickType_t start=xTaskGetTickCount(); uint32_t done=0;
    while (done<count) {
        int n=sending?lwip_send(fd,data+done,count-done,0):lwip_recv(fd,data+done,count-done,0);
        if (n>0) { done+=(uint32_t)n; }
        else if (n==0 || (errno!=EWOULDBLOCK && errno!=EAGAIN && errno!=EINTR)) { return false; }
        if ((TickType_t)(xTaskGetTickCount()-start)>=pdMS_TO_TICKS(SOCKET_MS)) { return false; }
        if (n<0) { vTaskDelay(1); }
    }
    return true;
}
static bool nonblocking(int fd)
{ int flags=lwip_fcntl(fd,F_GETFL,0); return flags>=0 && lwip_fcntl(fd,F_SETFL,flags|O_NONBLOCK)>=0; }
static bool respond(int fd, uint32_t op, uint32_t id, const response_t *r)
{
    uint8_t payload[SONAR_CONTROL_REPLY_BYTES],header[SONAR_CONTROL_HEADER];
    for (unsigned i=0;i<SONAR_CONTROL_REPLY_WORDS;++i) { sonar_put_u32(payload+4U*i,r->words[i]); }
    sonar_control_header(header,op|SONAR_CONTROL_RESPONSE,id,payload,sizeof(payload));
    return io(fd,header,sizeof(header),true) && io(fd,payload,sizeof(payload),true);
}
static void serve(int fd, uint32_t session)
{
    request_t q={.session=session}; response_t r;
    uint32_t last=0,token=0;
    if (!nonblocking(fd)) { return; }
    for (;;) {
        uint8_t header[SONAR_CONTROL_HEADER]; uint32_t op,id,bytes,crc;
        if (!io(fd,header,sizeof(header),false) || !sonar_control_decode(header,&op,&id,&bytes,&crc)) { break; }
        /* Never allow the wire client to invoke the internal cleanup opcode. */
        if (op<CTRL_GET_CAPABILITIES || op>CTRL_UPLOAD_CANCEL || id==0U || id<=last || token==UINT32_MAX) { break; }
        last=id; q.token=++token; q.opcode=op; q.bytes=bytes;
        if (!io(fd,q.data,bytes,false)) { break; }
        if (sonar_crc32(q.data,bytes)!=crc) {
            memset(&r,0,sizeof(r)); r.words[0]=CTRL_CHECKSUM;
        } else if (!rpc(&q,&r)) {
            memset(&r,0,sizeof(r)); r.words[0]=CTRL_TIMEOUT;
            (void)respond(fd,op,id,&r); break;
        }
        if (!respond(fd,op,id,&r)) { break; }
    }
    /* Cleanup is queued after every earlier request. A new connection cannot
     * accidentally inherit a pending configuration or incomplete upload. */
    q.opcode=CTRL_END_SESSION; q.bytes=0; q.token=++token;
    (void)rpc(&q,&r);
}
static void server(void *unused)
{
    (void)unused;
    int listener=lwip_socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in address;
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET;
    address.sin_port=htons(SONAR_CONTROL_PORT); address.sin_addr.s_addr=INADDR_ANY;
    if (listener<0 || !nonblocking(listener) ||
        lwip_bind(listener,(struct sockaddr *)&address,sizeof(address))<0 || lwip_listen(listener,1)<0) {
        if (listener>=0) { lwip_close(listener); }
        sonar_console_lock(); xil_printf("CTRL server failed to bind port 5003.\r\n"); sonar_console_unlock();
        vTaskDelete(NULL); return;
    }
    sonar_console_lock(); xil_printf("CTRL listening on TCP 5003; configuration never starts motion.\r\n"); sonar_console_unlock();
    uint32_t session=0;
    for (;;) {
        int fd=lwip_accept(listener,NULL,NULL);
        if (fd<0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (session==UINT32_MAX) { lwip_close(fd); continue; }
        serve(fd,++session); lwip_close(fd);
    }
}
bool sonar_control_server_start(void)
{
    if (started || requests==NULL || responses==NULL) { return false; }
    started=xTaskCreate(server,"sonar_control",2048,NULL,2,NULL)==pdPASS;
    return started;
}
