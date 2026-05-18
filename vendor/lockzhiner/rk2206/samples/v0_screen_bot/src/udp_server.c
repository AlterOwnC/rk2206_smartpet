#include "udp_server.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "los_task.h"
#include "cmsis_os2.h"
#include "lz_hardware.h"
#include "config_network.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"

#include "menu_ui.h"
#include "hardware_control.h"
#include "command_queue.h"

#define SERVER_PORT 6666
#define BUFF_LEN    256

void udp_server_thread(void *arg)
{
    WifiLinkedInfo info;
    int server_fd, ret;
    char buf[BUFF_LEN];
    struct sockaddr_in client_addr;
    socklen_t len;
    unsigned int my_ip = 0;

    printf("\n[UDP Server] Starting Wi-Fi Connection...\n");

    set_wifi_config_route_ssid(NULL, (uint8_t *)"鸿蒙研究院");
    set_wifi_config_route_passwd(NULL, (uint8_t *)"12345678");
    SetWifiModeOn();

    while (1) {
        if (GetLinkedInfo(&info) == WIFI_SUCCESS) {
            if (info.connState == WIFI_CONNECTED && info.ipAddress != 0) {
                my_ip = info.ipAddress;
                printf("[UDP Server] Connected! IP: %s\n",
                       inet_ntoa(info.ipAddress));
                break;
            }
        }
        LOS_Msleep(1000);
    }

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) return;

    int flag = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_PORT);

    ret = bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        close(server_fd);
        return;
    }

    struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("[UDP Server] Listening on %s:%d (timeout=3s)\n",
           inet_ntoa(my_ip), SERVER_PORT);

    int idle_count = 0;
    while (1)
    {
        memset(buf, 0, BUFF_LEN);
        len = sizeof(client_addr);
        memset(&client_addr, 0, sizeof(client_addr));

        int count = recvfrom(server_fd, buf, BUFF_LEN - 1, 0,
                             (struct sockaddr*)&client_addr, &len);

        if (count > 0)
        {
            idle_count = 0;
            printf("[UDP] RX %d bytes from %s:%d | %s\n",
                   count, inet_ntoa(client_addr.sin_addr.s_addr),
                   ntohs(client_addr.sin_port), buf);

            char *p_temp = strstr(buf, "\"temperature\":");
            char *p_hum  = strstr(buf, "\"humidity\":");
            char *p_wgt  = strstr(buf, "\"food_basin_weight\":");
            char *p_wat  = strstr(buf, "\"pet_water_basin_level_percent\":");

            if (p_temp || p_hum || p_wgt || p_wat)
            {
                SystemCommand cmd = {.type = CMD_SENSOR_UPDATE,
                                     .param1 = -1, .param2 = -1,
                                     .param3 = -1, .param4 = -1};

                if (p_temp) sscanf(p_temp, "\"temperature\":%d", &cmd.param1);
                if (p_hum)  sscanf(p_hum,  "\"humidity\":%d", &cmd.param2);
                if (p_wgt) {
                    float wf = 0.0f;
                    sscanf(p_wgt, "\"food_basin_weight\":%f", &wf);
                    cmd.param3 = (int)(wf + 0.5f);
                }
                if (p_wat)  sscanf(p_wat,  "\"pet_water_basin_level_percent\":%d", &cmd.param4);

                printf("[UDP] Parsed: T=%d H=%d W=%d WL=%d\n",
                       cmd.param1, cmd.param2, cmd.param3, cmd.param4);

                cmd_send(&cmd);
            }
        }
        else if (count < 0)
        {
            idle_count++;
            // 检查 IP 是否变了
            if (GetLinkedInfo(&info) == WIFI_SUCCESS
                && info.connState == WIFI_CONNECTED
                && info.ipAddress != 0
                && info.ipAddress != my_ip) {
                printf("[UDP] *** IP CHANGED: was %s, now %s ***\n",
                       inet_ntoa(my_ip), inet_ntoa(info.ipAddress));
                printf("[UDP] *** Sender must target %s:%d ***\n",
                       inet_ntoa(info.ipAddress), SERVER_PORT);
                my_ip = info.ipAddress;
            }
            if (idle_count % 10 == 1) {
                printf("[UDP] No data for %d sec (my IP=%s), sender may be offline\n",
                       idle_count * 3, inet_ntoa(my_ip));
            }
        }
    }
    close(server_fd);
}
