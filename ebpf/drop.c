#include "common.h"

#include <linux/if_ether.h>
#include <linux/swab.h>
#include <linux/pkt_cls.h>
#include <iproute2/bpf_elf.h>
#include <linux/ip.h>
#include "../../iproute2/include/bpf_api.h"

#ifndef __section
# define __section(NAME)                  \
	__attribute__((section(NAME), used))
#endif

#ifndef __inline
# define __inline                         \
	inline __attribute__((always_inline))
#endif

#ifndef lock_xadd
# define lock_xadd(ptr, val)              \
	((void)__sync_fetch_and_add(ptr, val))
#endif

#ifndef BPF_FUNC
# define BPF_FUNC(NAME, ...)              \
	(*NAME)(__VA_ARGS__) = (void *)BPF_FUNC_##NAME
#endif

#define bpf_htonl __builtin_bswap32
#define bpf_memcpy __builtin_memcpy


#define EGRESS_DIR 0
#define INGRESS_DIR 1
#define MAXELEM 2000


typedef struct cnt_pkt {
	uint32_t drop;
	uint32_t pass;
} pkt_count;

struct bpf_elf_map iface_map __section("maps") = {
	.type           = BPF_MAP_TYPE_HASH,
	.size_key       = sizeof(uint32_t),
	.size_value     = ETH_ALEN,
	.pinning        = PIN_GLOBAL_NS,
	.max_elem       = MAXELEM,
};


struct bpf_elf_map egress_iface_stat_map  __section("maps") = {
	.type           = BPF_MAP_TYPE_HASH,
	.size_key       = sizeof(uint32_t),
	.size_value     = sizeof(pkt_count),
	.pinning        = PIN_GLOBAL_NS,
	.max_elem       = MAXELEM,
};

struct bpf_elf_map ingress_iface_stat_map  __section("maps") = {
	.type           = BPF_MAP_TYPE_HASH,
	.size_key       = sizeof(uint32_t),
	.size_value     = sizeof(pkt_count),
	.pinning        = PIN_GLOBAL_NS,
	.max_elem       = MAXELEM,
};


/* helper functions called from eBPF programs */
static int (*bpf_trace_printk)(const char *fmt, int fmt_size, ...) =
	        (void *) BPF_FUNC_trace_printk;

static __inline int match_mac( struct __sk_buff *skb, uint32_t dir) {
	//char buf[] = "IRL in skb: %x %x %x\n";
	uint32_t *bytes;
	pkt_count *inf;

	void *data = (void *)(long)skb->data;
	void *data_end = (void *)(long)skb->data_end;
	struct ethhdr  *eth  = data;
	char buf2[] = "IRL Matches!!\n";
	uint32_t idx = skb->ifindex;	

	if(data_end < (void*)eth + sizeof(struct ethhdr))
		return TC_ACT_SHOT;

	if(dir==EGRESS_DIR){
	  inf = map_lookup_elem(&egress_iface_stat_map, &(idx));
	} else{
	  inf = map_lookup_elem(&ingress_iface_stat_map, &(idx));
	}
	
	if (!inf){
		//haven't found the stat-entry, unexpected behavior, let packet go through.
		return TC_ACT_OK;
	}

	bytes = map_lookup_elem(&iface_map, &(idx));
	if (bytes) {
		__u8 src_mac[ETH_ALEN];
		__u8 pkt_mac[ETH_ALEN];
		bpf_memcpy(src_mac, bytes, ETH_ALEN);
		bpf_memcpy(pkt_mac, eth->h_source, ETH_ALEN);
		/*bpf_trace_printk(buf1, sizeof(buf1), 
			src_mac[0],src_mac[1],src_mac[2]);
		bpf_trace_printk(buf1, sizeof(buf1), 
			src_mac[3],src_mac[4],src_mac[5]);*/
		/*bpf_trace_printk(buf, sizeof(buf1), 
			pkt_mac[0],pkt_mac[1],pkt_mac[2]);
		bpf_trace_printk(buf, sizeof(buf1), 
			pkt_mac[3],pkt_mac[4],pkt_mac[5]);*/

		//check if the MAC matches and return TC_ACT_OK
	       		if(src_mac[0] == pkt_mac[0] &&
	       		src_mac[1] == pkt_mac[1] &&
	       		src_mac[2] == pkt_mac[2] &&
	       		src_mac[3] == pkt_mac[3] &&
	       		src_mac[4] == pkt_mac[4] &&
	       		src_mac[5] == pkt_mac[5]){
				bpf_trace_printk(buf2, sizeof(buf2));
				if(idx < MAXELEM){
				  lock_xadd(&(inf->pass), 1);
				}
				return TC_ACT_OK;
				
			}
			if(idx < MAXELEM){
			  lock_xadd(&(inf->drop), 1);
			}
		        return TC_ACT_SHOT;
	}
	if(idx < MAXELEM){
	  lock_xadd(&(inf->pass), 1);
	}
	return TC_ACT_OK;
}

__section("classifier_egress_drop") int egress_drop(struct __sk_buff *skb) {
	return match_mac(skb, EGRESS_DIR);
}

__section("classifier_ingress_drop") int ingress_drop(struct __sk_buff *skb) {
	return match_mac(skb, INGRESS_DIR);
}
