
#include "fastpath.h"

#define NEIGH_TYPE_LOCAL        1
#define NEIGH_TYPE_REACHABLE    2
#define NEIGH_TYPE_UNRESOLVED   3

struct arp_entry {
    uint8_t type;
    uint8_t port_id;
    struct ether_addr nh_arp;
};

struct nh_entry {
    uint32_t nh_ip;
    uint32_t nh_iface;
};

/* Next Hop Table (NHT) */
struct nh_table {
    uint32_t nht_users[RTE_TABLE_LPM_MAX_NEXT_HOPS]
    struct nh_entry nht[RTE_TABLE_LPM_MAX_NEXT_HOPS];
} __rte_cache_aligned;

struct route_private {
    struct module *ipv4[ROUTE_MAX_LINK];
    struct module *ipv6[ROUTE_MAX_LINK];
    struct rte_lpm *lpm_tbl[FASTPATH_MAX_SOCKETS];
    struct rte_lpm6 *lpm6_tbl[FASTPATH_MAX_SOCKETS];
    struct nh_entry *default_entry;
    struct nh_table *nh_tbl[FASTPATH_MAX_SOCKETS];
} __rte_cache_aligned;

struct module *route_module;

static void neigh_init(struct module *ipfwd);
static void lpm_init(struct module *ipfwd);

void route_receive(struct rte_mbuf *m, 
    __rte_unused struct module *peer, struct module *ipfwd)
{
    uint8_t next_hop;
    uint32_t ip_dst;
    int socketid = rte_socket_id();
    struct ipv4_hdr *ipv4_hdr;
    struct ipv6_hdr *ipv6_hdr;
    struct neighbour *neigh;
    struct route_private *private = (struct route_private *)ipfwd->private;
    
    if (m->ol_flags & (PKT_RX_IPV4_HDR)) {
        ipv4_hdr = rte_pktmbuf_mtod(m, struct ipv4_hdr *);
        ip_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
        
        /* Find destination port */
        if (rte_lpm_lookup(private->lpm_tbl[socketid], ip_dst, &next_hop) == 0) {
            neigh = private->neigh_tbl[next_hop];
            if (neigh == NULL) {
                fastpath_log_debug("lpm entry for "NIPQUAD_FMT" not found, drop packet\n",
                    NIPQUAD(ipv4_hdr->dst_addr));
                rte_pktmbuf_free(m);
                return;
            }

            switch (neigh->type) {
            case NEIGH_TYPE_LOCAL:
                fastpath_log_debug("ipfwd receive proto %d packet, will be supported later\n",
                    ipv4_hdr->next_proto_id);
                rte_pktmbuf_free(m);
                break;

            case NEIGH_TYPE_REACHABLE:
                rte_memcpy(rte_pktmbuf_mtod(m, char *) - sizeof(struct ether_hdr), 
                    &neigh->hdr, 
                    sizeof(struct ether_hdr));
                SEND_PKT(m, ipfwd, private->ipv4[neigh->if_out], PKT_DIR_XMIT);
                break;

            default:
                rte_pktmbuf_free(m);
                break;
            }
        }
    } else if (m->ol_flags & (PKT_RX_IPV6_HDR | PKT_RX_IPV6_HDR_EXT)) {
        ipv6_hdr = rte_pktmbuf_mtod(m, struct ipv6_hdr *);

        /* Find destination port */
        if (rte_lpm6_lookup(private->lpm6_tbl[socketid], ipv6_hdr->dst_addr, &next_hop) == 0) {
            neigh = private->neigh_tbl[next_hop];

            if (neigh == NULL) {
                fastpath_log_debug("lpm6 entry for "NIP6_FMT" not found, drop packet\n",
                    NIP6(ipv6_hdr->dst_addr));
                rte_pktmbuf_free(m);
                return;
            }

            switch (neigh->type) {
            case NEIGH_TYPE_LOCAL:
                fastpath_log_debug("ipfwd receive proto %d packet, will be supported later\n",
                    ipv6_hdr->proto);
                rte_pktmbuf_free(m);
                break;

            case NEIGH_TYPE_REACHABLE:
                rte_memcpy(rte_pktmbuf_mtod(m, char *) - sizeof(struct ether_hdr), 
                    &neigh->hdr, 
                    sizeof(struct ether_hdr));
                SEND_PKT(m, ipfwd, private->ipv6[neigh->if_out], PKT_DIR_XMIT);
                break;

            default:
                rte_pktmbuf_free(m);
                break;
            }
        }
    }
}

void route_xmit(struct rte_mbuf *m, 
    __rte_unused struct module *peer, struct module *ipfwd)
{
    RTE_SET_USED(ipfwd);
    
    fastpath_log_error("no protocol installed for now, may be later\n");
    
    rte_pktmbuf_free(m);
}

void neigh_init(struct module *ipfwd)
{
    char s[64];
    int socketid;
    struct route_private *private = (struct route_private *)ipfwd->private;

    for (socketid = 0; socketid < FASTPATH_MAX_SOCKETS; socketid++) {
        if (fastpath_is_socket_used(socketid) == 0) {
            continue;
        }

        if (private->neigh_hash_tbl[socketid] != NULL) {
            continue;
        }

        struct rte_hash_parameters neigh_hash_params = {
            .name = NULL,
            .entries = FASTPATH_NEIGH_HASH_ENTRIES,
            .bucket_entries = 8,
            .key_len = sizeof(uint32_t),
            .hash_func_init_val = 0,
        };

        snprintf(s, sizeof(s), "neigh_hash_%d", socketid);
        neigh_hash_params.name = s;
        neigh_hash_params.socket_id = socketid;

        private->neigh_hash_tbl[socketid] = rte_hash_create(&neigh_hash_params);
        if (private->neigh_hash_tbl[socketid] == NULL) {
            rte_panic("Unable to create the neigh hash on socket %d\n", socketid);
            return;
        }

        fastpath_log_info("ipfwd create neigh hash table %s\n", neigh_hash_params.name);
    }
}

void lpm_init(struct module *ipfwd)
{
    char s[64];
    int socketid;
    unsigned lcore_id;
    struct rte_lpm *lpm;
    struct rte_lpm6 *lpm6;
    struct route_private *private = (struct route_private *)ipfwd->private;

    struct rte_lpm6_config lpm6_config = {
        .max_rules = FASTPATH_MAX_LPM6_RULES,
        .number_tbl8s = FASTPATH_LPM6_NUMBER_TBL8S,
        .flags = 0
    };

    for (lcore_id = 0; lcore_id < FASTPATH_MAX_LCORES; lcore_id++) {
        if (rte_lcore_is_enabled(lcore_id) == 0)
            continue;
        
        socketid = rte_lcore_to_socket_id(lcore_id);

        if (private->lpm_tbl[socketid] == NULL) {
            snprintf(s, sizeof(s), "route_lpm_%i", socketid);

            lpm = rte_lpm_create(s, socketid, FASTPATH_MAX_LPM_RULES, 0);
            if (lpm == NULL) {
                rte_panic("Cannot create LPM table\n");
                return;
            }
            private->lpm_tbl[socketid] = lpm;
        }

        if (private->lpm6_tbl[socketid] == NULL) {
            snprintf(s, sizeof(s), "route_lpm6_%i", socketid);

            lpm6 = rte_lpm6_create(s, socketid, &lpm6_config);
            if (lpm6 == NULL) {
                rte_panic("Cannot create LPM6 table\n");
                return;
            }
            private->lpm6_tbl[socketid] = lpm6;
        }
    }
}

int route_connect(struct module *local, struct module *peer, void *param)
{
    struct route_private *private;

    if (local == NULL || peer == NULL) {
        fastpath_log_error("route_connect: invalid local %p peer %p\n", 
            local, peer);
        return -EINVAL;
    }

    fastpath_log_info("route_connect: local %s peer %s\n", local->name, peer->name);

    private = (struct route_private *)local->private;

    if (peer->type == MODULE_TYPE_INTERFACE) {
        uint16_t ifidx = *(uint16_t *)param;
        if (ifidx >= route_MAX_LINK) {
            fastpath_log_error("route_connect: invalid ifidx %d\n", ifidx);
            return -EINVAL;
        }

        fastpath_log_info("route_connect: ipfwd add interface %d %s\n", ifidx, peer->name);
        
        private->ipv4[ifidx] = peer;

        peer->connect(peer, local, NULL);
    } else {
        fastpath_log_error("route_connect: invalid peer type %d\n", peer->type);
        return -ENOENT;
    }

    return 0;
}

struct module * route_init(void)
{
    struct module *ipfwd;
    struct route_private *private;

    ipfwd = rte_zmalloc(NULL, sizeof(struct module), 0);
    if (ipfwd == NULL) {
        fastpath_log_error("route_init: malloc module failed\n");
        return NULL;
    }

    private = rte_zmalloc(NULL, sizeof(struct route_private), 0);
    if (private == NULL) {
        rte_free(ipfwd);
        
        fastpath_log_error("route_init: malloc route_private failed\n");
        return NULL;
    }

    ipfwd->type = MODULE_TYPE_IPFWD;
    ipfwd->receive = route_receive;
    ipfwd->transmit = route_xmit;
    ipfwd->connect = route_connect;
    snprintf(ipfwd->name, sizeof(ipfwd->name), "ipfwd");

    ipfwd->private = private;

    neigh_init(ipfwd);
    lpm_init(ipfwd);

    route_module = ipfwd;

    return ipfwd;
}
