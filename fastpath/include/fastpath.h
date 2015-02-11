
#ifndef __FASTPATH_H__
#define __FASTPATH_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/queue.h>
#include <net/if.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include <linux/if_tun.h>
#include <poll.h>
#include <assert.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_tailq.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_hash.h>
#include <rte_ip_frag.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_lpm.h>
#include <rte_string_fns.h>
#include <rte_spinlock.h>

#include "main.h"
#include "log.h"
#include "utils.h"

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_ARG(x) ((u8*)(x))[0],((u8*)(x))[1],((u8*)(x))[2],((u8*)(x))[3],((u8*)(x))[4],((u8*)(x))[5]

#define NIPQUAD(addr) \
    ((unsigned char *)&addr)[0], \
    ((unsigned char *)&addr)[1], \
    ((unsigned char *)&addr)[2], \
    ((unsigned char *)&addr)[3]
#define NIPQUAD_FMT "%u.%u.%u.%u"

#define NIP6(addr) \
    ntohs((addr).s6_addr16[0]), \
    ntohs((addr).s6_addr16[1]), \
    ntohs((addr).s6_addr16[2]), \
    ntohs((addr).s6_addr16[3]), \
    ntohs((addr).s6_addr16[4]), \
    ntohs((addr).s6_addr16[5]), \
    ntohs((addr).s6_addr16[6]), \
    ntohs((addr).s6_addr16[7])
#define NIP6_FMT "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x"
#define NIP6_SEQFMT "%04x%04x%04x%04x%04x%04x%04x%04x"

#if defined(__LITTLE_ENDIAN)
#define HIPQUAD(addr) \
    ((unsigned char *)&addr)[3], \
    ((unsigned char *)&addr)[2], \
    ((unsigned char *)&addr)[1], \
    ((unsigned char *)&addr)[0]
#elif defined(__BIG_ENDIAN)
#define HIPQUAD     NIPQUAD
#else
#error "Please fix asm/byteorder.h"
#endif /* __LITTLE_ENDIAN */

enum {
    MODULE_TYPE_ETHERNET,
    MODULE_TYPE_VLAN,
    MODULE_TYPE_BRIDGE,
    MODULE_TYPE_INTERFACE,
};

struct module {
#define NAME_SIZE   16
    char name[NAME_SIZE];
    uint16_t ifindex;
    uint16_t type;
    void (*receive)(struct rte_mbuf *m, struct module *peer, struct module *local);
    void (*transmit)(struct rte_mbuf *m, struct module *peer, struct module *local);
    void *private;
};

#define SEND_PKT(m, local, peer) do { \
        if (unlikely((peer) == NULL)) { \
            fastpath_log_error("%s: peer not valid\n", __func__); \
            rte_pktmbuf_free((m)); \
        } else { \
            if (unlikely((peer)->receive == NULL)) { \
                fastpath_log_error("%s: peer receive not valid\n", __func__); \
                rte_pktmbuf_free((m)); \
            } else { \
                (peer)->receive((m), (local), (peer)); \
            } \
        } \
    } while (0)

#include "ethernet.h"
#include "vlan.h"
#include "bridge.h"
#include "interface.h"

#endif /* __FASTPATH_H__ */

