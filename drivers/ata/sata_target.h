#ifndef SATA_TARGET_H
#define SATA_TARGET_H

#include <linux/scatterlist.h>

#define ST_DATA_LEN	512
#define ST_QDEPTH	32

#define PAGE_SECTORS	(PAGE_SIZE >> 9)

struct target_sg_map {
	struct sg_table sgt;
	int dma_dir;
};

struct sata_target {
	struct rb_root rb_root;
	struct device *dev;
	sector_t sectors;
	struct target_sg_map *tsm;
	unsigned int depth;
	unsigned int max_segments;
	void *data;
	dma_addr_t data_handle;
	unsigned int data_len;
	struct ata_queued_cmd *active_qc;
	int wce;
};

struct sata_target *sata_target_init(struct device *, sector_t, unsigned int,
					unsigned int);
void sata_target_destroy(struct sata_target *);
int sata_target_map_sg(struct sata_target *, sector_t, unsigned int,
			unsigned char, int);
void sata_target_unmap_sg(struct sata_target *, unsigned char);
int sata_target_map_identify(struct sata_target *, u8 *);

static inline struct scatterlist *target_tag_to_sgl(struct sata_target *st,
						    unsigned int tag)
{
	BUG_ON(tag >= st->depth);

	return st->tsm[tag].sgt.sgl;
}

static inline void sata_target_wcache_set(struct sata_target *st, int enable)
{
	st->wce = !!enable;
}

#endif
