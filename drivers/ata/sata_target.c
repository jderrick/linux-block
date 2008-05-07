/*
 * Experimental sata target. It's meant to be used as a fast device for
 * testing IO stack behaviour. It is non-persistent across boots, as it
 * uses main memory as a backing store.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/rbtree.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

#include "sata_target.h"

/*
 * Not sure how to better test for this, just assume that a 64-bit arch
 * is sane and has an iommu by default
 */
#if BITS_PER_LONG == 64
static int have_iommu = 1;
#else
static int have_iommu;
#endif

module_param(have_iommu, int, 0444);
MODULE_PARM_DESC(have_iommu, "Assume IOMMU exists and works (0=no, 1=yes)");

struct page_extent {
	struct rb_node rb_node;
	struct page *pages;
	sector_t sector;
	unsigned int sectors;
};

struct sector_page {
	/*
	 * state
	 */
	struct page_extent *last_extent;

	/*
	 * output
	 */
	struct page *page;
	unsigned short offset;
	unsigned short length;
};

static void prune_pages(struct sata_target *st)
{
	struct rb_node *node;

	while ((node = rb_first(&st->rb_root)) != NULL) {
		struct page_extent *pe;
		int i, order;

		pe = rb_entry(node, struct page_extent, rb_node);
		rb_erase(&pe->rb_node, &st->rb_root);

		order = pe->sectors >> (PAGE_SHIFT - 9);
		for (i = 0; i < order; i++) {
			struct page *page = pe->pages + i;

			ClearPageReserved(page);
		}

		__free_pages(pe->pages, order);
		kfree(pe);
	}
}

/*
 * Free target device and page backing
 */
void sata_target_destroy(struct sata_target *st)
{
	int i;

	prune_pages(st);

	for (i = 0; i < st->depth; i++) {
		struct target_sg_map *tsm = &st->tsm[i];

		sg_free_table(&tsm->sgt);
	}

	dma_free_coherent(st->dev, ST_DATA_LEN, st->data, st->data_handle);
	kfree(st->tsm);
	kfree(st);
}
EXPORT_SYMBOL_GPL(sata_target_destroy);

static struct page_extent *pe_rb_find(struct sata_target *st, sector_t offset)
{
	struct rb_node *node = st->rb_root.rb_node;
	struct page_extent *pe;

	while (node) {
		pe = rb_entry(node, struct page_extent, rb_node);

		if (offset < pe->sector)
			node = node->rb_left;
		else if (offset >= pe->sector + pe->sectors)
			node = node->rb_right;
		else
			return pe;
	}

	return NULL;
}

static void pe_rb_insert(struct sata_target *st, struct page_extent *pe)
{
	struct rb_node **p, *parent;
	struct page_extent *__pe;

	p = &st->rb_root.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		__pe = rb_entry(parent, struct page_extent, rb_node);

		if (pe->sector < __pe->sector)
			p = &(*p)->rb_left;
		else if (pe->sector >= __pe->sector + __pe->sectors)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&pe->rb_node, parent, p);
	rb_insert_color(&pe->rb_node, &st->rb_root);
}

/*
 * Alloc page_extent structure, which maps a number of sequential pages
 * in our extrent rbtree, and add it to the target rb root.
 */
static int add_pages_to_st(struct sata_target *st, struct page *pages,
			    unsigned int nr_pages, sector_t offset)
{
	struct page_extent *pe;
	int i;

	pe = kzalloc(sizeof(*pe), GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

	for (i = 0; i < nr_pages; i++)
		SetPageReserved(pages + i);

	RB_CLEAR_NODE(&pe->rb_node);
	pe->pages = pages;
	pe->sector = offset;
	pe->sectors = nr_pages << (PAGE_SHIFT - 9);
	pe_rb_insert(st, pe);
	return 0;
}

/*
 * Always leave 256MB free
 */
#define RESERVE_MB	256

static int size_ok(unsigned long nr_pages)
{
	unsigned long reserve_pages, sys_pages;
	struct sysinfo si;

	si_meminfo(&si);
	sys_pages = si.totalram >> PAGE_SHIFT;
	reserve_pages = (RESERVE_MB * 1024UL * 1024UL) >> PAGE_SHIFT;
	if (sys_pages < reserve_pages)
		return 0;
	if (nr_pages > sys_pages - reserve_pages)
		return 0;

	return 1;
}

static int dma64_ok(struct device *dev)
{
	/*
	 * If the device only supports 32-bit dma, then set an appropriate
	 * gfp allocation mask. If it's a 64-bit dma capable device or we
	 * have an iommu, use all of memory
	 */
	if ((dev->dma_mask && *dev->dma_mask == DMA_64BIT_MASK) || have_iommu)
		return 1;

	return 0;
}

static int alloc_backing(struct sata_target *st, unsigned long nr_pages)
{
	unsigned long allocs[MAX_ORDER];
	unsigned long left;
	int i, alloc_order;
	sector_t offset;
	gfp_t gfp_mask;

	/*
	 * start with 2^5 order pages, we fallback to lower orders when
	 * it starts failing
	 */
	alloc_order = 5;

	if (dma64_ok(st->dev)) {
		printk(KERN_INFO "sata_target: using full 64-bit memory\n");
		gfp_mask = GFP_HIGHUSER;
	} else {
		printk(KERN_INFO "sata_target: using low 32-bit memory\n");
		gfp_mask = GFP_DMA32;
	}

	memset(allocs, 0, sizeof(allocs));
	offset = 0;
	left = nr_pages;
	while (left) {
		struct page *pages;

		/*
		 * Make sure we don't alloc too much, scale order down
		 */
		while ((1 << alloc_order) > left)
			alloc_order--;

		/*
		 * Alloc pages and/or adjust order on failure
		 */
		do {
			pages = alloc_pages(gfp_mask, alloc_order);
			if (pages) {
				allocs[alloc_order]++;
				break;
			}
			if (!alloc_order) {
				printk(KERN_ERR "sata_target: OOM. Got %lu of "
						"%lu pages\n", nr_pages - left,
								nr_pages);
				return 1;
			}
			alloc_order--;
		} while (1);

		if (add_pages_to_st(st, pages, 1 << alloc_order, offset)) {
			__free_pages(pages, alloc_order);
			return 1;
		}

		offset += PAGE_SECTORS;
		left -= 1 << alloc_order;
	}

	printk(KERN_INFO "sata_target: %lu backing pages:\n", nr_pages);
	i = MAX_ORDER;
	do {
		i--;
		if (allocs[i] == 0)
			continue;
		printk(KERN_INFO "  order%d: %lu pages\n", i, allocs[i]);
	} while (i);

	return 0;
}

/*
 * Initialize and allocate page backing for a target device
 */
struct sata_target *sata_target_init(struct device *dev, sector_t sectors,
				     unsigned int depth, unsigned int segments)
{
	struct sata_target *st;
	unsigned long nr_pages, i;

	nr_pages = (sectors + PAGE_SECTORS - 1) >> PAGE_SHIFT;
	if (!size_ok(nr_pages)) {
		printk(KERN_ERR "sata_target: %llu sectors is too large\n",
				(unsigned long long) sectors);
		return NULL;
	}

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return NULL;

	st->rb_root = RB_ROOT;
	st->dev = dev;
	st->depth = depth;
	st->max_segments = segments;
	st->wce = 1;

	st->tsm = kzalloc(depth * sizeof(struct target_sg_map), GFP_KERNEL);
	if (!st->tsm)
		goto err;

	st->data = dma_alloc_coherent(dev, ST_DATA_LEN, &st->data_handle,
								GFP_KERNEL);
	if (!st->data)
		goto err;
	st->data_len = ST_DATA_LEN;

	for (i = 0; i < depth; i++) {
		struct target_sg_map *tsm = &st->tsm[i];

		if (sg_alloc_table(&tsm->sgt, segments, GFP_KERNEL))
			goto err;
	}

	if (!alloc_backing(st, nr_pages)) {
		st->sectors = sectors;
		return st;
	}

err:
	sata_target_destroy(st);
	return NULL;
}
EXPORT_SYMBOL_GPL(sata_target_init);

static inline int sector_in_extent(struct page_extent *pe, sector_t sector)
{
	return (sector >= pe->sector) && (sector < (pe->sector + pe->sectors));
}

/*
 * We don't need any locking, as the list is never modified after it has
 * been set up.
 */
static int sector_to_page(struct sata_target *st, struct sector_page *sp,
			  sector_t sector)
{
	struct page_extent *pe;
	unsigned int page_off;
	sector_t pe_off, off;

	if (sp->last_extent && sector_in_extent(sp->last_extent, sector))
		pe = sp->last_extent;
	else {
		pe = pe_rb_find(st, sector);
		BUG_ON(!pe);
		sp->last_extent = pe;
	}

	off = (sector - pe->sector) << 9;
	pe_off = off >> PAGE_SHIFT;
	page_off = off & ~PAGE_MASK;

	sp->page = pe->pages + pe_off;
	sp->offset = page_off;
	sp->length = PAGE_SIZE - page_off;
	return 0;
}

/*
 * Fill the sg list with the pages corresponding to 'sector' and forward.
 */
int sata_target_map_sg(struct sata_target *st, sector_t sector,
		       unsigned int nr_sectors, unsigned char tag, int ddir)
{
	struct target_sg_map *tsm;
	struct scatterlist *sg;
	struct sector_page sp;
	unsigned int i, nents;

	BUG_ON(tag >= st->depth);

	tsm = &st->tsm[tag];
	nents = 0;
	memset(&sp, 0, sizeof(sp));
	for_each_sg(tsm->sgt.sgl, sg, st->max_segments, i) {
		if (!nr_sectors)
			break;
		if (sector_to_page(st, &sp, sector))
			return -EINVAL;
		if (sp.length > (nr_sectors << 9))
			sp.length = nr_sectors << 9;

		sg_set_page(sg, sp.page, sp.length, sp.offset);
		nr_sectors -= (sp.length >> 9);
		sector += (sp.length >> 9);
		nents++;
	}

	if (nr_sectors)
		printk(KERN_ERR "sata_target: weird, segments too small?\n");

	tsm->sgt.nents = 0;
	if (nents) {
		tsm->sgt.nents = dma_map_sg(st->dev, tsm->sgt.sgl, nents, ddir);
		if (!tsm->sgt.nents) {
			printk(KERN_ERR "sata_target: dma map returned 0\n");
			return -EAGAIN;
		}
	}

	tsm->dma_dir = ddir;
	return nents;
}
EXPORT_SYMBOL_GPL(sata_target_map_sg);

void sata_target_unmap_sg(struct sata_target *st, unsigned char tag)
{
	struct target_sg_map *tsm = &st->tsm[tag];

	BUG_ON(tsm->dma_dir == -1);
	dma_unmap_sg(st->dev, tsm->sgt.sgl, tsm->sgt.nents, tsm->dma_dir);
	tsm->dma_dir = -1;
}
EXPORT_SYMBOL_GPL(sata_target_unmap_sg);

static void pad_copy(char *dst, char *src, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (*src)
			dst[i] = *src++;
		else
			dst[i] = ' ';
	}
}

/*
 * Fill IDENTIFY data as a sata disk
 */
static void target_fill_identify(struct sata_target *st)
{
	u16 *p = st->data;
	u16 cyls, heads, spt;

	heads = 255;
	spt = 63;
	cyls = st->sectors / (heads * spt);

	memset(p, 0, ST_DATA_LEN);
	p[1] = cpu_to_le16(cyls);
	p[3] = cpu_to_le16(heads);
	p[6] = cpu_to_le16(spt);
	pad_copy((char *)(p + 10), "LINUXSATATARGET", 20);
	pad_copy((char *)(p + 23), "1.00", 8);
	pad_copy((char *)(p + 27), "LINUX TARGET DISK", 40);
	p[49] = cpu_to_le16((1 << 9) | (1 << 8));	/* lba and dma supp */
	p[50] = cpu_to_le16(1 << 14);
	/* word88 is valid */
	p[53] = cpu_to_le16(1 << 2);
	p[60] = cpu_to_le16(st->sectors);
	p[61] = cpu_to_le16(st->sectors >> 16);
	p[75] = cpu_to_le16(st->depth - 1);		/* queue depth */
	/* supports 1.5gbps, 3.0gbps, and ncq */
	p[76] = cpu_to_le16((1 << 1) | (1 << 2) | (1 << 8));
	p[80] = cpu_to_le16(0xf0);	/* ata4 -> ata7 supported */
	p[81] = cpu_to_le16(0x16);
	/* write cache supported */
	p[82] = cpu_to_le16(1 << 5);
	/* supports flush cache (and ext) and 48 bit addressing */
	p[83] = cpu_to_le16((1 << 14) || (1 << 13) | (1 << 12) | (1 << 10));
	/* supports FUA */
	p[84] = cpu_to_le16((1 << 14) | (1 << 6));
	/* write cache enabled? */
	if (st->wce)
		p[85] = cpu_to_le16(1 << 5);
	/* supports flush cache (and ext) and 48 bit addressing */
	p[86] = cpu_to_le16((1 << 13) | (1 << 12) | (1 << 10));
	p[87] = cpu_to_le16(1 << 14);
	/* udma5 set, and udma5 and below are supported */
	p[88] = cpu_to_le16((1 << 5) | (1 << 13));
#if 0
	p[93] = cpu_to_le16((1 << 14) | 1);
#endif
	p[100] = cpu_to_le16(st->sectors);
	p[101] = cpu_to_le16(st->sectors >> 16);
	p[102] = cpu_to_le16(st->sectors >> 32);
	p[103] = cpu_to_le16(st->sectors >> 48);
}

int sata_target_map_identify(struct sata_target *st, u8 *tag)
{
	struct target_sg_map *tsm;
	struct scatterlist *sg;
	int i;

	target_fill_identify(st);

	/*
	 * always use tag 0, the device should be idle now
	 */
	*tag = 0;
	tsm = &st->tsm[0];

	for_each_sg(tsm->sgt.sgl, sg, 1, i)
		sg_set_buf(sg, st->data, st->data_len);

	tsm->dma_dir = DMA_FROM_DEVICE;
	tsm->sgt.nents = dma_map_sg(st->dev, tsm->sgt.sgl, 1, tsm->dma_dir);
	return tsm->sgt.nents;
}
EXPORT_SYMBOL_GPL(sata_target_map_identify);
