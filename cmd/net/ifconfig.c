/* See LICENSE file for copyright and license details. */
#include "util.h"
#include "arg.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static void
usage(void)
{
	eprintf("usage: %s [-a] [interface [action ...]]\n", argv0);
}

static void
print_ipv6(const char *name)
{
	FILE *fp = fopen("/proc/net/if_inet6", "r");
	char line[256];
	char addr_hex[40];
	char ifname[IFNAMSIZ];
	unsigned int ifindex, plen, scope, flags;
	int i, j;
	char ipv6_str[40];
	const char *scope_str;

	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "%32s %x %x %x %x %15s", addr_hex, &ifindex, &plen, &scope, &flags, ifname) == 6) {
			if (strcmp(name, ifname) == 0) {
				j = 0;
				for (i = 0; i < 32; i++) {
					ipv6_str[j++] = addr_hex[i];
					if (i > 0 && i < 31 && (i % 4 == 3))
						ipv6_str[j++] = ':';
				}
				ipv6_str[j] = '\0';

				scope_str = "Unknown";
				switch (scope) {
				case 0x00: scope_str = "Global"; break;
				case 0x10: scope_str = "Host"; break;
				case 0x20: scope_str = "Link"; break;
				case 0x40: scope_str = "Site"; break;
				case 0x80: scope_str = "Compat"; break;
				}
				printf("        inet6 addr: %s/%d Scope:%s\n", ipv6_str, plen, scope_str);
			}
		}
	}
	fclose(fp);
}

static void
display_interface(int sock, const char *name)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;
	unsigned char *hwaddr;
	FILE *fp;
	char line[256];
	char *p, *ifname;
	short flags;
	int mtu = 0;
	int metric = 0;
	unsigned long long rx_bytes, rx_packets, rx_errs, rx_drop;
	unsigned long long tx_bytes, tx_packets, tx_errs, tx_drop;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

	if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
		weprintf("ioctl SIOCGIFFLAGS %s:\n", name);
		return;
	}
	flags = ifr.ifr_flags;

	printf("%-9s ", name);

	if (ioctl(sock, SIOCGIFHWADDR, &ifr) >= 0) {
		hwaddr = (unsigned char *)ifr.ifr_hwaddr.sa_data;
		printf("Link encap:");
		switch (ifr.ifr_hwaddr.sa_family) {
		case ARPHRD_ETHER:
			printf("Ethernet  HWaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
			       hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
			break;
		case ARPHRD_LOOPBACK:
			printf("Local Loopback\n");
			break;
		default:
			printf("UNSPEC\n");
			break;
		}
	} else {
		printf("Link encap:UNSPEC\n");
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(sock, SIOCGIFADDR, &ifr) >= 0) {
		sin = (struct sockaddr_in *)&ifr.ifr_addr;
		printf("        inet addr:%s", inet_ntoa(sin->sin_addr));

		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
		if (ioctl(sock, SIOCGIFBRDADDR, &ifr) >= 0) {
			sin = (struct sockaddr_in *)&ifr.ifr_broadaddr;
			printf("  Bcast:%s", inet_ntoa(sin->sin_addr));
		}

		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
		if (ioctl(sock, SIOCGIFNETMASK, &ifr) >= 0) {
			sin = (struct sockaddr_in *)&ifr.ifr_netmask;
			printf("  Mask:%s", inet_ntoa(sin->sin_addr));
		}
		printf("\n");
	}

	print_ipv6(name);

	printf("        ");
	if (flags & IFF_UP) printf("UP ");
	if (flags & IFF_BROADCAST) printf("BROADCAST ");
	if (flags & IFF_DEBUG) printf("DEBUG ");
	if (flags & IFF_LOOPBACK) printf("LOOPBACK ");
	if (flags & IFF_POINTOPOINT) printf("POINTOPOINT ");
	if (flags & IFF_RUNNING) printf("RUNNING ");
	if (flags & IFF_NOARP) printf("NOARP ");
	if (flags & IFF_PROMISC) printf("PROMISC ");
	if (flags & IFF_ALLMULTI) printf("ALLMULTI ");
	if (flags & IFF_MULTICAST) printf("MULTICAST ");

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(sock, SIOCGIFMTU, &ifr) >= 0)
		mtu = ifr.ifr_mtu;
	if (ioctl(sock, SIOCGIFMETRIC, &ifr) >= 0)
		metric = ifr.ifr_metric;
	printf(" MTU:%d  Metric:%d\n", mtu, metric);

	fp = fopen("/proc/net/dev", "r");
	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			p = strchr(line, ':');
			if (p) {
				*p = '\0';
				ifname = line;
				while (isspace(*ifname))
					ifname++;
				if (strcmp(ifname, name) == 0) {
					if (sscanf(p + 1, "%llu %llu %llu %llu %*u %*u %*u %*u %llu %llu %llu %llu",
					           &rx_bytes, &rx_packets, &rx_errs, &rx_drop,
					           &tx_bytes, &tx_packets, &tx_errs, &tx_drop) == 8) {
						printf("        RX packets:%llu errors:%llu dropped:%llu overruns:0 frame:0\n",
						       rx_packets, rx_errs, rx_drop);
						printf("        TX packets:%llu errors:%llu dropped:%llu overruns:0 carrier:0\n",
						       tx_packets, tx_errs, tx_drop);
						printf("        RX bytes:%llu  TX bytes:%llu\n", rx_bytes, tx_bytes);
					}
					break;
				}
			}
		}
		fclose(fp);
	}
	printf("\n");
}

static void
list_interfaces(int sock, int all)
{
	FILE *fp = fopen("/proc/net/dev", "r");
	struct ifreq ifr;
	char line[256];
	char *p, *name;
	int line_num = 0;

	if (!fp)
		eprintf("fopen /proc/net/dev:\n");

	while (fgets(line, sizeof(line), fp)) {
		line_num++;
		if (line_num <= 2)
			continue;
		p = strchr(line, ':');
		if (p) {
			*p = '\0';
			name = line;
			while (isspace(*name))
				name++;
			memset(&ifr, 0, sizeof(ifr));
			strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) >= 0) {
				if (all || (ifr.ifr_flags & IFF_UP)) {
					display_interface(sock, name);
				}
			}
		}
	}
	fclose(fp);
}

int
main(int argc, char *argv[])
{
	struct ifreq ifr;
	struct sockaddr_in *sin;
	char *name;
	char *arg;
	char *slash;
	int aflag = 0;
	int sock = -1;
	int i = 1;
	int prefix;
	unsigned int mask;

	ARGBEGIN {
	case 'a':
		aflag = 1;
		break;
	default:
		usage();
	} ARGEND

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		eprintf("socket:\n");

	if (argc == 0) {
		list_interfaces(sock, aflag);
		close(sock);
		return 0;
	}

	name = argv[0];

	if (argc == 1) {
		display_interface(sock, name);
		close(sock);
		return 0;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);

	while (i < argc) {
		arg = argv[i];

		if (strcmp(arg, "up") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "down") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_UP;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "netmask") == 0) {
			if (i + 1 >= argc) eprintf("netmask needs an address\n");
			sin = (struct sockaddr_in *)&ifr.ifr_netmask;
			sin->sin_family = AF_INET;
			if (inet_pton(AF_INET, argv[i + 1], &sin->sin_addr) <= 0)
				eprintf("invalid address: %s\n", argv[i + 1]);
			if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) eprintf("ioctl SIOCSIFNETMASK:");
			i += 2;
		} else if (strcmp(arg, "broadcast") == 0) {
			if (i + 1 >= argc) eprintf("broadcast needs an address\n");
			sin = (struct sockaddr_in *)&ifr.ifr_broadaddr;
			sin->sin_family = AF_INET;
			if (inet_pton(AF_INET, argv[i + 1], &sin->sin_addr) <= 0)
				eprintf("invalid address: %s\n", argv[i + 1]);
			if (ioctl(sock, SIOCSIFBRDADDR, &ifr) < 0) eprintf("ioctl SIOCSIFBRDADDR:");
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_BROADCAST;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i += 2;
		} else if (strcmp(arg, "mtu") == 0) {
			if (i + 1 >= argc) eprintf("mtu needs a value\n");
			ifr.ifr_mtu = atoi(argv[i + 1]);
			if (ioctl(sock, SIOCSIFMTU, &ifr) < 0) eprintf("ioctl SIOCSIFMTU:");
			i += 2;
		} else if (strcmp(arg, "metric") == 0) {
			if (i + 1 >= argc) eprintf("metric needs a value\n");
			ifr.ifr_metric = atoi(argv[i + 1]);
			if (ioctl(sock, SIOCSIFMETRIC, &ifr) < 0) eprintf("ioctl SIOCSIFMETRIC:");
			i += 2;
		} else if (strcmp(arg, "txqueuelen") == 0) {
			if (i + 1 >= argc) eprintf("txqueuelen needs a value\n");
			ifr.ifr_qlen = atoi(argv[i + 1]);
			if (ioctl(sock, SIOCSIFTXQLEN, &ifr) < 0) eprintf("ioctl SIOCSIFTXQLEN:");
			i += 2;
		} else if (strcmp(arg, "dstaddr") == 0 || strcmp(arg, "pointopoint") == 0) {
			if (i + 1 >= argc) eprintf("%s needs an address\n", arg);
			sin = (struct sockaddr_in *)&ifr.ifr_dstaddr;
			sin->sin_family = AF_INET;
			if (inet_pton(AF_INET, argv[i + 1], &sin->sin_addr) <= 0)
				eprintf("invalid address: %s\n", argv[i + 1]);
			if (ioctl(sock, SIOCSIFDSTADDR, &ifr) < 0) eprintf("ioctl SIOCSIFDSTADDR:");
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_POINTOPOINT;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i += 2;
		} else if (strcmp(arg, "arp") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_NOARP;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-arp") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_NOARP;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "promisc") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_PROMISC;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-promisc") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_PROMISC;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "allmulti") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_ALLMULTI;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-allmulti") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_ALLMULTI;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "multicast") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_MULTICAST;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else if (strcmp(arg, "-multicast") == 0) {
			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags &= ~IFF_MULTICAST;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");
			i++;
		} else {
			sin = (struct sockaddr_in *)&ifr.ifr_addr;
			sin->sin_family = AF_INET;
			slash = strchr(arg, '/');
			prefix = -1;
			if (slash) {
				*slash = '\0';
				prefix = atoi(slash + 1);
			}
			if (inet_pton(AF_INET, arg, &sin->sin_addr) <= 0) {
				eprintf("unknown action or invalid address: %s\n", arg);
			}
			if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) eprintf("ioctl SIOCSIFADDR:");

			if (prefix >= 0) {
				mask = 0;
				if (prefix > 0)
					mask = htonl(~0U << (32 - prefix));
				sin = (struct sockaddr_in *)&ifr.ifr_netmask;
				sin->sin_family = AF_INET;
				sin->sin_addr.s_addr = mask;
				if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) eprintf("ioctl SIOCSIFNETMASK:");
			}

			if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCGIFFLAGS:");
			ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
			if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) eprintf("ioctl SIOCSIFFLAGS:");

			i++;
		}
	}

	close(sock);
	return 0;
}
