/* 
 * AMD K8 NUMA support.
 * Discover the memory map and associated nodes.
 * 
 * Doesn't use the ACPI SRAT table because it has a questionable license.
 * Instead the northbridge registers are read directly. 
 * 
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * $Id: k8topology.c,v 1.7 2003/04/02 21:36:22 ak Exp $
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/module.h>
#include <asm/io.h>
#include <linux/pci_ids.h>
#include <asm/types.h>
#include <asm/mmzone.h>
#include <asm/proto.h>
#include <asm/e820.h>
#include <asm/pci-direct.h>

int memnode_shift;
u8  memnodemap[NODEMAPSIZE];

static __init int find_northbridge(void)
{
	int num; 

	for (num = 0; num < 32; num++) { 
		u32 header;
		
		header = read_pci_config(0, num, 0, 0x00);  
		if (header != (PCI_VENDOR_ID_AMD | (0x1100<<16)))
			continue; 	

		header = read_pci_config(0, num, 1, 0x00); 
		if (header != (PCI_VENDOR_ID_AMD | (0x1101<<16)))
			continue;	
		return num; 
	} 

	return -1; 	
}

#define MAXNODE 8 
#define NODEMASK 0xff

struct node { 
	u64 start,end; 
};

#define for_all_nodes(n) \
	for (n=0; n<MAXNODE;n++) if (nodes[n].start!=nodes[n].end)

static int __init compute_hash_shift(struct node *nodes)
{
	int i; 
	int shift = 20;
	u64 addr, maxend = 0UL;
	
	for (i = 0; i < MAXNODE; i++)
		if ((nodes[i].start != nodes[i].end) && (nodes[i].end > maxend))
			maxend = nodes[i].end;

	while ((1UL << shift) < (maxend / NODEMAPSIZE))
		shift++;

	memset(memnodemap, 0xff, sizeof(*memnodemap) * NODEMAPSIZE);
	for (i = 0; i < MAXNODE; i++) {
		if (nodes[i].start == nodes[i].end)
			continue;
		for (addr = nodes[i].start;
		     addr < nodes[i].end;
		     addr += (1UL << shift)) {
			if (memnodemap[addr >> shift] != 0xff) {
				printk(KERN_INFO
	"Your memory is not aligned - you need to rebuild your kernel "
	"with a bigger NODEMAPSIZE (shift=%d addr=%lx)\n",
					shift, addr);
				return -1;
			} 
			memnodemap[addr >> shift] = i;
		} 
	} 
	return shift;
}

extern unsigned long nodes_present;
extern unsigned long end_pfn;
int numa_nodes;

int __init k8_scan_nodes(unsigned long start, unsigned long end)
{ 
	unsigned long prevbase;
	struct node nodes[MAXNODE];
	u32 reg;
	int nodeid, maxnode, i, nb;

	nb = find_northbridge(); 
	if (nb < 0) 
		return nb;

	printk(KERN_INFO "Scanning NUMA topology in Northbridge %d\n", nb); 

	reg = read_pci_config(0, nb, 0, 0x60);
	numa_nodes = ((reg >> 4) & 7) + 1;
	printk(KERN_INFO "Number of nodes: %d (%x)\n", numa_nodes, reg);

	memset(&nodes,0,sizeof(nodes)); 
	prevbase = 0;
	maxnode = -1; 
	for (i = 0; i < MAXNODE; i++) { 
		unsigned long base,limit; 

		base = read_pci_config(0, nb, 1, 0x40 + i*8);
		limit = read_pci_config(0, nb, 1, 0x44 + i*8);

		nodeid = limit & 7;

		if ((base & 3) == 0) {
			if (i < numa_nodes)
				printk(KERN_INFO "Skipping disabled node %d\n", i);
			continue;
		}
		if (nodeid >= numa_nodes) {
			printk(KERN_INFO "Ignoring excess node %d (%lx:%lx)\n", nodeid,
				base, limit);
			continue;
		}

		if (!limit) { 
			printk(KERN_INFO "Skipping node entry %d (base %lx)\n", i,			       base);
			continue;
		}
		if ((base >> 8) & 3 || (limit >> 8) & 3) {
			printk(KERN_ERR "Node %d using interleaving mode %lx/%lx\n", 
			       nodeid, (base>>8)&3, (limit>>8) & 3); 
			return -1; 
		}	
		if (nodeid > maxnode) 
			maxnode = nodeid; 
		if ((1UL << nodeid) & nodes_present) { 
			printk("Node %d already present. Skipping\n", nodeid);
			continue;
		}

		limit >>= 16; 
		limit <<= 24; 
		limit |= (1<<24)-1;

		if (limit > end_pfn << PAGE_SHIFT) 
			limit = end_pfn << PAGE_SHIFT; 
		if (limit <= base)
			continue; 
			
		base >>= 16;
		base <<= 24; 

		if (base < start) 
			base = start; 
		if (limit > end) 
			limit = end; 
		if (limit == base) 
			continue; 
		if (limit < base) { 
			printk(KERN_INFO"Node %d bogus settings %lx-%lx. Ignored.\n",
			       nodeid, base, limit); 			       
			continue; 
		} 
		
		/* Could sort here, but pun for now. Should not happen anyroads. */
		if (prevbase > base) { 
			printk(KERN_INFO "Node map not sorted %lx,%lx\n",
			       prevbase,base);
			return -1;
		}
			
		printk(KERN_INFO "Node %d MemBase %016lx Limit %016lx\n", 
		       nodeid, base, limit); 
		
		nodes[nodeid].start = base; 
		nodes[nodeid].end = limit;

		prevbase = base;
	} 

	if (maxnode <= 0)
		return -1; 

	memnode_shift = compute_hash_shift(nodes);
	if (memnode_shift < 0) { 
		printk(KERN_ERR "No NUMA node hash function found. Contact maintainer\n"); 
		return -1; 
	} 
	printk(KERN_INFO "Using node hash shift of %d\n", memnode_shift); 

	for_all_nodes(i) { 
		setup_node_bootmem(i, nodes[i].start, nodes[i].end); 
	} 

	return 0;
} 

