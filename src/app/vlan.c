
#include "fastpath.h"
#include "vlan.h"

struct vlan_private {
    uint16_t vid;
    uint16_t reserved;
    struct module *ethernet;
    struct module *bridge;
};

struct module *vlan_modules[VLAN_VID_MAX];

void vlan_receive(struct rte_mbuf *m, __rte_unused struct module *peer, struct module *vlan)
{
    struct vlan_private *private = (struct vlan_private *)vlan->private;

    rte_pktmbuf_adj(m, (uint16_t)sizeof(struct vlan_hdr));
    memmove(rte_pktmbuf_mtod(m, char *) - sizeof(struct ether_hdr), 
        rte_pktmbuf_mtod(m, char *) - sizeof(struct ether_hdr) - sizeof(struct vlan_hdr), 
        2 * sizeof(struct ether_addr));

    SEND_PKT(m, vlan, private->bridge, PKT_DIR_RECV);
    
    return;
}

void vlan_xmit(struct rte_mbuf *m, __rte_unused struct module *peer, struct module *vlan)
{
    struct ether_hdr *eth_hdr;
    struct vlan_hdr  *vlan_hdr;
    struct vlan_private *private = (struct vlan_private *)vlan->private;
    
    rte_pktmbuf_prepend(m, (uint16_t)sizeof(struct vlan_hdr));
    memmove(rte_pktmbuf_mtod(m, void *), 
        rte_pktmbuf_mtod(m, char *) + sizeof(struct vlan_hdr), 
        2 * sizeof(struct ether_addr));
    eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
    vlan_hdr = (struct vlan_hdr *)(eth_hdr + 1);
    vlan_hdr->vlan_tci = rte_cpu_to_be_16(private->vid);
    vlan_hdr->eth_proto = rte_cpu_to_be_16(ETHER_TYPE_VLAN);    
    
    SEND_PKT(m, vlan, private->ethernet, PKT_DIR_XMIT);
    
    return;
}

int vlan_set_ethernet(struct module *vlan, struct module *eth)
{
    struct vlan_private *private;

    if (vlan == NULL || eth == NULL) {
        fastpath_log_error("vlan_set_ethernet: invalid vlan %p ethernet %p\n", 
            vlan, eth);

        return -EINVAL;
    }
    
    private = (struct vlan_private *)vlan->private;
    private->ethernet = eth;

    return 0;
}

int vlan_connect(struct module *local, struct module *peer, void *param)
{
    struct bridge_private *private;

    if (local == NULL || peer == NULL) {
        fastpath_log_error("vlan_connect: invalid local %p peer %p\n", 
            local, peer);
        return -EINVAL;
    }
    
    fastpath_log_info("vlan_connect: local %s peer %s\n", local->name, peer->name);

    private = local->private;

    if (peer->type == MODULE_TYPE_BRIDGE) {
        private->bridge = peer;
    } else if (peer->type == MODULE_TYPE_ETHERNET) {
        private->ethernet = peer;

        peer->connect(peer, local, NULL);
    } else {
        fastpath_log_error("vlan_connect: invalid peer type %d\n", peer->type);
        return -ENOENT;
    }

    return 0;
}

int vlan_init(uint16_t vid)
{
    struct module *vlan;
    struct vlan_private *private;

    if (vid > VLAN_VID_MASK) {
        fastpath_log_error("vlan_init: invalid vid %d\n", vid);
        return -EINVAL;
    }
    
    vlan = rte_zmalloc(NULL, sizeof(struct module), 0);
    if (vlan == NULL) {
        fastpath_log_error("vlan_init: malloc module failed\n");
        return -ENOMEM;
    }

    private = rte_zmalloc(NULL, sizeof(struct vlan_private), 0);
    if (private == NULL) {
        rte_free(vlan);
        
        fastpath_log_error("vlan_init: malloc module failed\n");
        return -ENOMEM;
    }

    vlan->type = MODULE_TYPE_VLAN;
    snprintf(vlan->name, sizeof(vlan->name), "vlan%d", vid);
    
    private->vid = vid;
    
    vlan->private = (void *)private;

    vlan_modules[vid] = vlan;

    return 0;
}

