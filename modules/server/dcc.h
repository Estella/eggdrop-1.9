#ifndef _SERVER_DCC_H_
#define _SERVER_DCC_H_

/* Statistic types for dcc_send_info(). */
#define DCC_SEND_SENT	1
#define DCC_SEND_LEFT	2
#define DCC_SEND_CPS_TOTAL	3
#define DCC_SEND_CPS_SNAPSHOT	4
#define DCC_SEND_ACKS	5
#define DCC_SEND_BYTES_ACKED	6
#define DCC_SEND_RESUMED_AT	7
#define DCC_SEND_REQUEST_TIME	8
#define DCC_SEND_CONNECT_TIME	9

int dcc_dns_set(const char *host);
int dcc_start_chat(const char *nick, int timeout);
int dcc_start_send(const char *nick, const char *fname, int timeout);
int dcc_send_info(int idx, int field, void *valueptr);
int dcc_accept_send(char *nick, char *localfname, char *fname, int size, int resume, char *ip, int port, int timeout);

extern bind_list_t ctcp_dcc_binds[];

#endif
