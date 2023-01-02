// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/netlink.h>
#include <linux/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*********************************************************************
 *
 * compile:
 *     aarch64-linux-gnu-gcc unit_test.c -o unit_test --static
 *
 * run:
 *     Copy the unit_test file to the /data/local/tmp directory,
 *     add the executable permission, and run the following command
 *     ./unit_test
 *
 *********************************************************************/

/* FIXME: BYHP */
#define NETLINK_HW_IAWARE_CPU 31
#define MAX_DATA_LEN 20

enum {
	CPU_HIGH_LOAD = 1,
	PROC_FORK = 2,
	PROC_COMM = 3,
	PROC_AUX_COMM = 4,
	PROC_LOAD = 5,
	PROC_AUX_COMM_FORK = 6,
	PROC_AUX_COMM_REMOVE = 7,
	CPUSET_ADJUST_BG = 8
};

enum {
	DEFAULT = 0,
	LOW_LOAD = 1,
	HIGH_LOAD = 2,
};

#define BG_CPU_0_7		1
#define BG_CPU_0_3		0

int main(void)
{
	int fd;
	unsigned int len;
	struct sockaddr_nl src, dst;
	struct msghdr msg;
	struct nlmsghdr *nlh, *nlh1, *nlh2 = NULL;
	struct iovec iov[1];
	int *p;
	int sock_no, cnt;

	fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_HW_IAWARE_CPU);
	if (fd == -1) {
		printf("netlink create fail\n");
		return -1;
	}

	memset(&src, 0, sizeof(struct sockaddr_nl));
	memset(&dst, 0, sizeof(struct sockaddr_nl));
	memset(&msg, 0, sizeof(struct msghdr));

	src.nl_family = AF_NETLINK;
	src.nl_pid = getpid();
	src.nl_groups = 0;

	dst.nl_family = AF_NETLINK;
	dst.nl_pid = 0;
	dst.nl_groups = 0;

	bind(fd, (struct sockaddr *)&src, sizeof(struct sockaddr_nl));
	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_DATA_LEN));

	for (;;) {
		memset(nlh, 0, NLMSG_SPACE(MAX_DATA_LEN));
		nlh->nlmsg_len = NLMSG_SPACE(MAX_DATA_LEN);
		nlh->nlmsg_pid = getpid();

		nlh->nlmsg_type = NETLINK_HW_IAWARE_CPU;
		nlh->nlmsg_flags = NLM_F_REQUEST;
		iov[0].iov_base = (void *)nlh;
		iov[0].iov_len = nlh->nlmsg_len;
		msg.msg_name = (void *)&dst;
		msg.msg_namelen = sizeof(struct sockaddr_nl);
		msg.msg_iov = &iov[0];
		msg.msg_iovlen = 1;

		/* send */
		sendmsg(fd, &msg, 0);

		memset(nlh, 0, NLMSG_SPACE(MAX_DATA_LEN));
		nlh->nlmsg_len = NLMSG_SPACE(MAX_DATA_LEN);
		iov[0].iov_base = (void *)nlh;
		iov[0].iov_len = nlh->nlmsg_len;
		msg.msg_iov = &iov[0];
		msg.msg_iovlen = 1;

		/* recv */
		len = recvmsg(fd, &msg, 0);
		p = (int *)NLMSG_DATA(nlh);
		sock_no = p[0];
		cnt = p[1];

		switch (sock_no) {
		case CPU_HIGH_LOAD:
			printf("CPU_HIGH_LOAD: %s\n",
				p[2] == HIGH_LOAD ? "HIGH_LOAD" : "LOW_LOAD");
		break;
		case CPUSET_ADJUST_BG:
			printf("CPUSET_ADJUST_BG: %s\n",
				p[1] == BG_CPU_0_7 ? "BG_CPU_0_7" : "BG_CPU_0_3");
		break;

		default:
			printf("TODO : %d\n", sock_no);
		break;
		}
	}

	close(fd);
	return 0;
}


