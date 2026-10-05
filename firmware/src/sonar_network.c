#include "sonar_stage2.h"
#include "sonar_console.h"
#include "sonar_processing.h"
#include "sonar_clock.h"
#include "sonar_control_server.h"
#include "sonar_tcp_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "netif/xadapter.h"
#include "xparameters.h"
#include "xil_printf.h"
#include <string.h>
/* Use the error values and storage used by this BSP's socket implementation.
 * Including Newlib errno.h here redirects reads to __errno(), but AMD's
 * LWIP_PROVIDE_ERRNO build writes the global lwIP errno instead. */
#include "lwip/errno.h"

#if !LWIP_SOCKET || NO_SYS
#error "Enable lwip220 SOCKET_API in the FreeRTOS domain and rebuild the platform."
#endif
#if LWIP_PROVIDE_ERRNO && defined(errno)
#error "Do not mix Newlib errno with the BSP's global lwIP socket errno."
#endif
#if INCLUDE_vTaskPrioritySet != 1 || INCLUDE_uxTaskPriorityGet != 1
#error "Enable FreeRTOS task priority get/set for background Ethernet initialization."
#endif
static bool connected;
static struct netif ethernet;
static int discovery_fd=-1;
bool sonar_network_connected(void)
{ bool ready; taskENTER_CRITICAL(); ready=connected; taskEXIT_CRITICAL(); return ready; }
static void connection(bool value)
{ taskENTER_CRITICAL(); connected=value; taskEXIT_CRITICAL(); }
static void note(const char *s)
{ sonar_console_lock(); xil_printf("ETH %s\r\n",s); sonar_console_unlock(); }
static bool nonblocking(int fd)
{ unsigned long on=1; return lwip_ioctl(fd,FIONBIO,&on)==0; }

/* Discovery is read-only: it reports the board address, never changes either
 * computer's network configuration. All socket calls stay in this task. */
static void discovery_start(void)
{
    struct sockaddr_in address;
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET;
    address.sin_port=htons(SONAR_NET_DISCOVERY_PORT); address.sin_addr.s_addr=INADDR_ANY;
    discovery_fd=lwip_socket(AF_INET,SOCK_DGRAM,0);
    if (discovery_fd<0 || !nonblocking(discovery_fd) ||
        lwip_bind(discovery_fd,(struct sockaddr *)&address,sizeof(address))<0) {
        if (discovery_fd>=0) { lwip_close(discovery_fd); }
        discovery_fd=-1; note("discovery unavailable; use the printed IP with --board.");
    }
}
static void discovery_poll(void)
{
    if (discovery_fd<0) { return; }
    for (unsigned budget=0;budget<4U;++budget) {
        uint8_t request[32],reply[16]={'S','O','N','A','R','I','P','2'};
        struct sockaddr_in peer;
        socklen_t length=sizeof(peer);
        int n=lwip_recvfrom(discovery_fd,request,sizeof(request),0,(struct sockaddr *)&peer,&length);
        if (n<0) { return; }
        if (n!=16 || memcmp(request,"SONARWHO",8)!=0) { continue; }
        memcpy(reply+8,request+8,8); /* Echo nonce; response source is the board IP. */
        (void)lwip_sendto(discovery_fd,reply,sizeof(reply),0,(struct sockaddr *)&peer,length);
    }
}

/* One bounded deadline covers all partial writes/reads of a record. */
static void transfer_fault(const char *part, const char *reason, int error, uint32_t done)
{
    sonar_console_lock();
    xil_printf("ETH %s: %s error=%d bytes=%u\r\n",part,reason,error,(unsigned)done);
    sonar_console_unlock();
}
static bool transfer(int fd, uint8_t *data, uint32_t length, bool send_data,
                     TickType_t began, const char *part)
{
    uint32_t done=0;
    while (done<length) {
        if ((TickType_t)(xTaskGetTickCount()-began)>=pdMS_TO_TICKS(SONAR_NET_DEADLINE_MS)) {
            transfer_fault(part,"deadline expired",0,done); return false;
        }
        int n=send_data?lwip_send(fd,data+done,length-done,0):lwip_recv(fd,data+done,length-done,0);
        int error=n<0?errno:0;
        if (n>0) { done+=(uint32_t)n; }
        else if (n==0 || (error!=EWOULDBLOCK && error!=EAGAIN && error!=EINTR)) {
            transfer_fault(part,n==0?"peer closed":"socket failure",error,done); return false;
        }
        else { discovery_poll(); vTaskDelay(1); }
    }
    return true;
}
static bool send_record(int fd, int slot, uint64_t batch_us)
{
    /* Empty records mark the start of every batch, including idle seconds. */
    sonar_frame_meta_t meta={.trigger_us=batch_us};
    uint32_t bytes=0,crc=0;
    uint8_t *payload=NULL;

    if (slot>=0) {
        uint32_t pcm_frames=0;
        meta=sonar_frames.slots[slot].meta;

        if (!sonar_processing_make_wav(sonar_frame_data[slot],
                                       SONAR_STAGE2_BYTES,
                                       &payload,
                                       &bytes,
                                       &pcm_frames)) {
            note("PDM-to-WAV processing failed; frame retained.");
            return false;
        }

        meta.flags |= SONAR_FRAME_FLAG_WAV_PCM16;
        crc=sonar_crc32(payload,bytes);
    }

    uint8_t header[SONAR_WIRE_HEADER],ack[12],expected[12]={'A','C','K','2'};
    sonar_frame_header(header,&meta,bytes,crc);
    memcpy(expected+4,header+12,4);
    memcpy(expected+8,header+72,4);

    TickType_t began=xTaskGetTickCount();
    if (!transfer(fd,header,sizeof(header),true,began,"header") ||
        (slot>=0 && !transfer(fd,payload,bytes,true,began,"payload")) ||
        !transfer(fd,ack,sizeof(ack),false,began,"ACK")) {
        return false;
    }

    if (memcmp(ack,expected,sizeof(ack))!=0) {
        note("invalid ACK; frame retained.");
        return false;
    }
    return true;
}
static void serve(int fd)
{
    uint8_t hello[8],reply[8]={'R','E','A','D','Y','2','\r','\n'};
    TickType_t began=xTaskGetTickCount();
    if (!nonblocking(fd) || !transfer(fd,hello,8,false,began,"hello") || memcmp(hello,"SONAR2\r\n",8)!=0 ||
        !transfer(fd,reply,8,true,began,"ready")) { return; }
    connection(true); note("receiver connected; data batches every 30 seconds, ACK required before buffer reuse.");
    TickType_t wake=xTaskGetTickCount();
    for (;;) {
        while ((TickType_t)(xTaskGetTickCount()-wake)<pdMS_TO_TICKS(SONAR_STAGE2_BATCH_MS)) {
            discovery_poll();
            /* Detect a closed receiver during the idle interval so reconnects
             * do not wait behind an abandoned connection for 30 seconds. */
            uint8_t unexpected;
            int n=lwip_recv(fd,&unexpected,1,MSG_PEEK);
            int error=n<0?errno:0;
            if (n>=0 || (error!=EWOULDBLOCK && error!=EAGAIN && error!=EINTR)) {
                note("receiver closed or sent unexpected data between batches."); return;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        wake+=pdMS_TO_TICKS(SONAR_STAGE2_BATCH_MS);
        taskENTER_CRITICAL(); uint32_t through=sonar_frames_latest(&sonar_frames); taskEXIT_CRITICAL();
        uint64_t batch_us=sonar_clock_us();
        if (!send_record(fd,-1,batch_us)) { return; }
        unsigned sent=0;
        for (;;) {
            taskENTER_CRITICAL(); int slot=sonar_frames_take(&sonar_frames,through); taskEXIT_CRITICAL();
            if (slot<0) { break; }
            bool ok=send_record(fd,slot,batch_us);
            taskENTER_CRITICAL(); (void)sonar_frames_finish(&sonar_frames,(unsigned)slot,ok); taskEXIT_CRITICAL();
            if (!ok) { return; }
            ++sent;
        }
        sonar_console_lock();
        xil_printf("ETH batch complete: %u capture(s) saved and acknowledged\r\n",sent);
        sonar_console_unlock();
        /* Avoid a burst of missed periodic releases after a slow batch. */
        if ((TickType_t)(xTaskGetTickCount()-wake)>=pdMS_TO_TICKS(SONAR_STAGE2_BATCH_MS)) { wake=xTaskGetTickCount(); }
    }
}
void sonar_network_task(void *unused)
{
    (void)unused;
    UBaseType_t running_priority=uxTaskPriorityGet(NULL);
    /* AMD's PHY setup uses xiltimer busy waits during autonegotiation.
     * Run those waits below the heartbeat producer so a normal link startup
     * cannot trip its deadline. The AMD link-monitor task also uses priority 0. */
    vTaskPrioritySet(NULL,tskIDLE_PRIORITY);
    ip_addr_t ip,mask,gateway;
    unsigned char mac[6]=SONAR_NET_MAC;
    lwip_init();
    if (!ipaddr_aton(SONAR_NET_IP,&ip) || !ipaddr_aton(SONAR_NET_MASK,&mask) ||
        !ipaddr_aton(SONAR_NET_GATEWAY,&gateway) ||
        xemac_add(&ethernet,&ip,&mask,&gateway,mac,XPAR_XEMACPS_0_BASEADDR)==NULL) {
        note("initialization failed; captures can fill DDR, then acquisition waits for free slots."); vTaskDelete(NULL); return;
    }
    netif_set_default(&ethernet); netif_set_up(&ethernet);
    vTaskPrioritySet(NULL,running_priority);
    if (sys_thread_new("eth_input",(void (*)(void *))xemacif_input_thread,&ethernet,2048,2)==NULL) {
        note("input task allocation failed."); vTaskDelete(NULL); return;
    }
    if (!sonar_tcp_console_start()) { note("TCP console creation failed; inspect UART diagnostics."); }
    if (!sonar_control_server_start()) { note("control-server task creation failed; scan configuration unavailable."); }
    int listener=lwip_socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in address;
    memset(&address,0,sizeof(address)); address.sin_family=AF_INET;
    address.sin_port=htons(SONAR_NET_PORT); address.sin_addr.s_addr=INADDR_ANY;
    if (listener<0 || !nonblocking(listener) || lwip_bind(listener,(struct sockaddr *)&address,sizeof(address))<0 ||
        lwip_listen(listener,1)<0) {
        if (listener>=0) { lwip_close(listener); }
        note("server setup failed."); vTaskDelete(NULL); return;
    }
    note("listening on " SONAR_NET_IP ":5001; subnet mask " SONAR_NET_MASK ".");
    discovery_start();
    for (;;) {
        discovery_poll();
        int fd=lwip_accept(listener,NULL,NULL);
        if (fd<0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        serve(fd); connection(false); lwip_close(fd);
        note("receiver disconnected; frames retained; acquisition continues while DDR slots are free.");
    }
}
