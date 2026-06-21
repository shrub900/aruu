/* See LICENSE file for copyright and license details. */
#include "util.h"
#include "diskutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

struct MbrPartition {
	uint8_t boot;
	uint8_t start_chs[3];
	uint8_t type;
	uint8_t end_chs[3];
	uint32_t start_lba;
	uint32_t size;
} __attribute__((packed));

struct MbrHeader {
	uint8_t code[446];
	struct MbrPartition parts[4];
	uint8_t sig[2];
} __attribute__((packed));

struct GptHeader {
	uint8_t sig[8];
	uint32_t rev;
	uint32_t size;
	uint32_t crc;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable;
	uint64_t last_usable;
	uint8_t disk_guid[16];
	uint64_t partition_lba;
	uint32_t num_parts;
	uint32_t part_size;
	uint32_t parts_crc;
} __attribute__((packed));

struct GptPartition {
	uint8_t type_guid[16];
	uint8_t part_guid[16];
	uint64_t start_lba;
	uint64_t end_lba;
	uint64_t flags;
	uint16_t name[36];
} __attribute__((packed));

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void
init_crc32_table(void)
{
	uint32_t i, j, c;
	for (i = 0; i < 256; i++) {
		c = i;
		for (j = 0; j < 8; j++) {
			if (c & 1)
				c = 0xedb88320U ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc32_table[i] = c;
	}
	crc32_table_initialized = 1;
}

static uint32_t
crc32(uint32_t crc, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	if (!crc32_table_initialized)
		init_crc32_table();
	while (len--)
		crc = (crc >> 8) ^ crc32_table[(crc ^ *p++) & 0xFF];
	return crc;
}

static int opt_list = 0;
static int opt_print = 0;
static int opt_init_gpt = 0;
static int opt_init_mbr = 0;
static int opt_add = 0;
static int opt_del = 0;
static int opt_part_idx = -1;
static uint64_t opt_start = 0;
static uint64_t opt_end = 0;
static const char *opt_type = NULL;

static void
usage(void)
{
	eprintf("usage: %s [-l] [-p] [-g] [-m] [-a] [-d] [-n index] [-b start] [-e end] [-t type] [device]\n", argv0);
}

static void
print_mbr(struct MbrHeader *mbr, const char *path)
{
	int i;
	printf("Disk: %s\n", path);
	printf("Partition table (MBR/dos):\n");
	printf("%-5s %-4s %-12s %-12s %-4s\n", "Index", "Boot", "Start", "Size", "Type");
	for (i = 0; i < 4; i++) {
		struct MbrPartition *p = &mbr->parts[i];
		if (p->size == 0)
			continue;
		printf("%-5d %-4s %-12u %-12u 0x%02x\n",
		       i + 1,
		       p->boot == 0x80 ? "Yes" : "No",
		       p->start_lba,
		       p->size,
		       p->type);
	}
}

static void
print_gpt(struct GptHeader *hdr, struct GptPartition *parts, const char *path)
{
	uint32_t i;
	char guid[37];
	printf("Disk: %s\n", path);
	printf("Partition table (GPT):\n");
	printf("%-5s %-12s %-12s %-36s\n", "Index", "Start", "End", "Type GUID");
	for (i = 0; i < hdr->num_parts; i++) {
		struct GptPartition *p = &parts[i];
		if (p->start_lba == 0 && p->end_lba == 0)
			continue;

		snprintf(guid, sizeof(guid),
		         "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		         p->type_guid[3], p->type_guid[2], p->type_guid[1], p->type_guid[0],
		         p->type_guid[5], p->type_guid[4],
		         p->type_guid[7], p->type_guid[6],
		         p->type_guid[8], p->type_guid[9],
		         p->type_guid[10], p->type_guid[11], p->type_guid[12], p->type_guid[13], p->type_guid[14], p->type_guid[15]);

		printf("%-5d %-12llu %-12llu %-36s\n",
		       i + 1,
		       (unsigned long long)p->start_lba,
		       (unsigned long long)p->end_lba,
		       guid);
	}
}

static int
read_gpt(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts)
{
	unsigned char sector[512];
	size_t part_sectors;

	if (blockdev_read(dev, 1, sector, 1) < 0)
		return -1;

	memcpy(hdr, sector, sizeof(*hdr));

	if (memcmp(hdr->sig, "EFI PART", 8) != 0)
		return -1;

	part_sectors = (hdr->num_parts * hdr->part_size + dev->sec_size - 1) / dev->sec_size;
	if (blockdev_read(dev, hdr->partition_lba, parts, part_sectors) < 0)
		return -1;

	return 0;
}

static int
write_gpt(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts)
{
	unsigned char sector[512];
	size_t part_sectors = (hdr->num_parts * hdr->part_size + dev->sec_size - 1) / dev->sec_size;

	/* update partition entries crc */
	hdr->parts_crc = crc32(~0U, parts, hdr->num_parts * hdr->part_size) ^ ~0U;

	/* calculate header crc (zeroing the crc field first) */
	hdr->crc = 0;
	hdr->crc = crc32(~0U, hdr, hdr->size) ^ ~0U;

	/* write protective mbr to sector 0 */
	struct MbrHeader pmbr;
	memset(&pmbr, 0, sizeof(pmbr));
	pmbr.parts[0].type = 0xEE;
	pmbr.parts[0].start_lba = 1;
	uint64_t limit = dev->size / dev->sec_size - 1;
	pmbr.parts[0].size = limit > 0xFFFFFFFFU ? 0xFFFFFFFFU : (uint32_t)limit;
	pmbr.sig[0] = 0x55;
	pmbr.sig[1] = 0xAA;
	if (blockdev_write(dev, 0, &pmbr, 1) < 0)
		return -1;

	/* write primary header and partition entries */
	memset(sector, 0, sizeof(sector));
	memcpy(sector, hdr, sizeof(*hdr));
	if (blockdev_write(dev, 1, sector, 1) < 0)
		return -1;
	if (blockdev_write(dev, hdr->partition_lba, parts, part_sectors) < 0)
		return -1;

	/* write backup partition entries and backup header */
	struct GptHeader backup_hdr = *hdr;
	backup_hdr.current_lba = hdr->backup_lba;
	backup_hdr.backup_lba = hdr->current_lba;
	backup_hdr.partition_lba = backup_hdr.current_lba - part_sectors;

	backup_hdr.crc = 0;
	backup_hdr.crc = crc32(~0U, &backup_hdr, backup_hdr.size) ^ ~0U;

	if (blockdev_write(dev, backup_hdr.partition_lba, parts, part_sectors) < 0)
		return -1;

	memset(sector, 0, sizeof(sector));
	memcpy(sector, &backup_hdr, sizeof(backup_hdr));
	if (blockdev_write(dev, backup_hdr.current_lba, sector, 1) < 0)
		return -1;

	return 0;
}

static int
do_print(const char *path)
{
	struct BlockDev dev;
	struct MbrHeader mbr;
	struct GptHeader gpt_hdr;
	struct GptPartition gpt_parts[128];

	if (blockdev_open(&dev, path, 0) < 0) {
		weprintf("cannot open %s:", path);
		return -1;
	}

	if (read_gpt(&dev, &gpt_hdr, gpt_parts) == 0) {
		print_gpt(&gpt_hdr, gpt_parts, path);
	} else {
		if (blockdev_read(&dev, 0, &mbr, 1) == 0 && mbr.sig[0] == 0x55 && mbr.sig[1] == 0xAA) {
			print_mbr(&mbr, path);
		} else {
			printf("Disk %s: unpartitioned or unknown format\n", path);
		}
	}

	blockdev_close(&dev);
	return 0;
}

static int
do_fdisk(const char *path)
{
	struct BlockDev dev;
	struct MbrHeader mbr;
	struct GptHeader gpt_hdr;
	struct GptPartition gpt_parts[128];
	int has_gpt = 0;

	if (blockdev_open(&dev, path, 1) < 0) {
		weprintf("cannot open %s:", path);
		return -1;
	}

	has_gpt = (read_gpt(&dev, &gpt_hdr, gpt_parts) == 0);

	if (opt_init_mbr) {
		memset(&mbr, 0, sizeof(mbr));
		mbr.sig[0] = 0x55;
		mbr.sig[1] = 0xAA;
		if (blockdev_write(&dev, 0, &mbr, 1) < 0) {
			weprintf("cannot write MBR to %s:", path);
			blockdev_close(&dev);
			return -1;
		}
		printf("Initialized MBR partition table on %s\n", path);
		blockdev_close(&dev);
		return 0;
	}

	if (opt_init_gpt) {
		memset(&gpt_hdr, 0, sizeof(gpt_hdr));
		memcpy(gpt_hdr.sig, "EFI PART", 8);
		gpt_hdr.rev = 0x00010000;
		gpt_hdr.size = 92;
		gpt_hdr.current_lba = 1;
		gpt_hdr.backup_lba = dev.size / dev.sec_size - 1;
		gpt_hdr.first_usable = 34;
		gpt_hdr.last_usable = gpt_hdr.backup_lba - 34;
		gpt_hdr.partition_lba = 2;
		gpt_hdr.num_parts = 128;
		gpt_hdr.part_size = 128;
		memset(gpt_parts, 0, sizeof(gpt_parts));

		if (write_gpt(&dev, &gpt_hdr, gpt_parts) < 0) {
			weprintf("cannot write GPT to %s:", path);
			blockdev_close(&dev);
			return -1;
		}
		printf("Initialized GPT partition table on %s\n", path);
		blockdev_close(&dev);
		return 0;
	}

	if (opt_add) {
		if (opt_part_idx < 1) {
			weprintf("add requires partition index (-n)\n");
			blockdev_close(&dev);
			return -1;
		}

		if (has_gpt) {
			if (opt_part_idx > 128) {
				weprintf("GPT partition index must be 1-128\n");
				blockdev_close(&dev);
				return -1;
			}
			struct GptPartition *p = &gpt_parts[opt_part_idx - 1];
			p->start_lba = opt_start;
			p->end_lba = opt_end;
			/* default type guid: linux filesystem data */
			p->type_guid[0] = 0xAF; p->type_guid[1] = 0x3D; p->type_guid[2] = 0xC6; p->type_guid[3] = 0x0F;
			p->type_guid[4] = 0x83; p->type_guid[5] = 0x84;
			p->type_guid[6] = 0x72; p->type_guid[7] = 0x47;
			p->type_guid[8] = 0x8E; p->type_guid[9] = 0x79;
			p->type_guid[10] = 0x3D; p->type_guid[11] = 0x69; p->type_guid[12] = 0xD8; p->type_guid[13] = 0x47; p->type_guid[14] = 0x7D; p->type_guid[15] = 0xE4;

			if (write_gpt(&dev, &gpt_hdr, gpt_parts) < 0) {
				weprintf("cannot update GPT on %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			printf("Added GPT partition %d to %s\n", opt_part_idx, path);
		} else {
			if (blockdev_read(&dev, 0, &mbr, 1) < 0) {
				weprintf("cannot read MBR from %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			if (opt_part_idx > 4) {
				weprintf("MBR partition index must be 1-4\n");
				blockdev_close(&dev);
				return -1;
			}
			struct MbrPartition *p = &mbr.parts[opt_part_idx - 1];
			p->start_lba = (uint32_t)opt_start;
			p->size = (uint32_t)(opt_end - opt_start + 1);
			p->type = opt_type ? (uint8_t)strtoul(opt_type, NULL, 0) : 0x83;
			mbr.sig[0] = 0x55;
			mbr.sig[1] = 0xAA;

			if (blockdev_write(&dev, 0, &mbr, 1) < 0) {
				weprintf("cannot update MBR on %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			printf("Added MBR partition %d to %s\n", opt_part_idx, path);
		}
	}

	if (opt_del) {
		if (opt_part_idx < 1) {
			weprintf("delete requires partition index (-n)\n");
			blockdev_close(&dev);
			return -1;
		}

		if (has_gpt) {
			if (opt_part_idx > 128) {
				weprintf("GPT partition index must be 1-128\n");
				blockdev_close(&dev);
				return -1;
			}
			memset(&gpt_parts[opt_part_idx - 1], 0, sizeof(struct GptPartition));
			if (write_gpt(&dev, &gpt_hdr, gpt_parts) < 0) {
				weprintf("cannot update GPT on %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			printf("Deleted GPT partition %d from %s\n", opt_part_idx, path);
		} else {
			if (blockdev_read(&dev, 0, &mbr, 1) < 0) {
				weprintf("cannot read MBR from %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			if (opt_part_idx > 4) {
				weprintf("MBR partition index must be 1-4\n");
				blockdev_close(&dev);
				return -1;
			}
			memset(&mbr.parts[opt_part_idx - 1], 0, sizeof(struct MbrPartition));
			if (blockdev_write(&dev, 0, &mbr, 1) < 0) {
				weprintf("cannot update MBR on %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			printf("Deleted MBR partition %d from %s\n", opt_part_idx, path);
		}
	}

	blockdev_close(&dev);
	return 0;
}

// ?man fdisk: partition table manipulator
// ?man arguments: [-l] [-p] [-g] [-m] [-a] [-d] [-n index] [-b start] [-e end] [-t type] [device]
// ?man fdisk performs partition table operations for MBR and GPT disks
int
main(int argc, char *argv[])
{
	ARGBEGIN {
	// ?man -l:list partitions of all devices
	case 'l':
		opt_list = 1;
		break;
	// ?man -p:print partition table of specified device
	case 'p':
		opt_print = 1;
		break;
	// ?man -g:initialize disk with GPT
	case 'g':
		opt_init_gpt = 1;
		break;
	// ?man -m:initialize disk with MBR
	case 'm':
		opt_init_mbr = 1;
		break;
	// ?man -a:add partition
	case 'a':
		opt_add = 1;
		break;
	// ?man -d:delete partition
	case 'd':
		opt_del = 1;
		break;
	// ?man -n:partition index
	case 'n':
		opt_part_idx = (int)estrtol(EARGF(usage()), 10);
		break;
	// ?man -b:starting sector LBA
	case 'b':
		opt_start = (uint64_t)estrtoul(EARGF(usage()), 10);
		break;
	// ?man -e:ending sector LBA
	case 'e':
		opt_end = (uint64_t)estrtoul(EARGF(usage()), 10);
		break;
	// ?man -t:partition type (MBR hex or GPT string)
	case 't':
		opt_type = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	if (opt_list) {
		struct BlockDevInfo *list = blockdev_get_list();
		struct BlockDevInfo *curr;
		if (!list)
			return 1;
		for (curr = list; curr; curr = curr->next) {
			char devpath[128];
			snprintf(devpath, sizeof(devpath), "/dev/%s", curr->name);
			do_print(devpath);
		}
		blockdev_free_list(list);
		return 0;
	}

	if (argc != 1)
		usage();

	if (opt_print) {
		return do_print(argv[0]) < 0 ? 1 : 0;
	}

	return do_fdisk(argv[0]) < 0 ? 1 : 0;
}
