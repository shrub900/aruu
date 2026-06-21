/* See LICENSE file for copyright and license details. */
#include "util.h"
#include "diskutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

struct FsType {
	const char *name;
	size_t magic_offset;
	size_t magic_len;
	uint64_t magic1;
	uint64_t magic2;
	size_t uuid_offset;
	size_t uuid_len;
	size_t label_offset;
	size_t label_len;
};

static const struct FsType fstypes[] = {
	/* ext2/3/4 */
	{ "ext", 1080, 2, 0xEF53, 0x53EF, 1128, 16, 1144, 16 },
	/* vfat / fat32 */
	{ "vfat", 82, 5, 0x3233544146ULL, 0, 67, 4, 71, 11 },
	/* vfat / fat12/16 */
	{ "vfat", 54, 4, 0x31544146, 0, 39, 4, 43, 11 },
	/* ntfs */
	{ "ntfs", 3, 4, 0x5346544e, 0, 72, 8, 0, 0 },
	/* xfs */
	{ "xfs", 0, 4, 0x42534658, 0x58465342, 32, 16, 108, 12 },
	/* btrfs */
	{ "btrfs", 65600, 8, 0x4D5F53665248425FULL, 0, 65803, 16, 65819, 256 },
	/* f2fs */
	{ "f2fs", 1024, 4, 0xF2F52010, 0x1020F5F2, 1132, 16, 1148, 512 },
	/* ufs1 */
	{ "ufs1", 9564, 4, 0x011954, 0x54190100, 0, 0, 0, 0 },
	/* ufs2 */
	{ "ufs2", 66908, 4, 0x19540119, 0x19015419, 67056, 16, 67072, 32 },
	/* fossil */
	{ "fossil", 131200, 4, 0x2340A3B1, 0xB1A34023, 0, 0, 131072, 127 },
	/* cwfs */
	{ "cwfs", 0, 4, 0xc1a551f5, 0xf551a5c1, 0, 0, 0, 0 },
};

static char *oflag = "full";
static char *sflag = NULL;
static int Uflag = 0;
static int Lflag = 0;

static void
usage(void)
{
	eprintf("usage: %s [-o format] [-s tag] [-U] [-L] [device ...]\n", argv0);
}

static void
format_uuid(const unsigned char *buf, size_t len, const char *type, char *out, size_t out_len)
{
	size_t i;
	char *p = out;

	if (strcmp(type, "vfat") == 0) {
		snprintf(out, out_len, "%02X%02X-%02X%02X", buf[3], buf[2], buf[1], buf[0]);
		return;
	}

	if (strcmp(type, "ntfs") == 0) {
		for (i = 8; i > 0; i--) {
			snprintf(p, out_len - (p - out), "%02X", buf[i - 1]);
			p += 2;
		}
		return;
	}

	for (i = 0; i < len; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10) {
			*p++ = '-';
		}
		snprintf(p, out_len - (p - out), "%02x", buf[i]);
		p += 2;
	}
	*p = '\0';
}

static void
print_tag(const char *devname, const char *tag, const char *value)
{
	if (sflag && strcasecmp(sflag, tag) != 0)
		return;

	if (strcmp(oflag, "value") == 0) {
		printf("%s\n", value);
	} else if (strcmp(oflag, "export") == 0) {
		printf("%s=%s\n", tag, value);
	} else {
		printf(" %s=\"%s\"", tag, value);
	}
}

static int
detect_zfs(const unsigned char *buf, size_t len)
{
	size_t offset;
	uint64_t magic;

	for (offset = 131072; offset + 8 <= len; offset += 1024) {
		magic = ((uint64_t)buf[offset + 7] << 56) |
		        ((uint64_t)buf[offset + 6] << 48) |
		        ((uint64_t)buf[offset + 5] << 40) |
		        ((uint64_t)buf[offset + 4] << 32) |
		        ((uint64_t)buf[offset + 3] << 24) |
		        ((uint64_t)buf[offset + 2] << 16) |
		        ((uint64_t)buf[offset + 1] << 8)  |
		        (uint64_t)buf[offset];

		if (magic == 0x00bab10caULL || magic == 0x0cb1ba0000000000ULL ||
		    magic == 0x00bab10cULL || magic == 0x0cb1ba00ULL)
			return 1;
	}
	return 0;
}

static int
detect_plan9_other(const unsigned char *buf, size_t len, const char **name)
{
	if (len >= 4) {
		uint32_t magic = ((uint32_t)buf[3] << 24) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[1] << 8) | (uint32_t)buf[0];
		if (magic == 0x686a6673 || magic == 0x73666a68) {
			*name = "hjfs";
			return 1;
		}
		if (magic == 0x67656673 || magic == 0x73666567) {
			*name = "gefs";
			return 1;
		}
	}
	if (len >= 516) {
		uint32_t magic = ((uint32_t)buf[515] << 24) | ((uint32_t)buf[514] << 16) | ((uint32_t)buf[513] << 8) | (uint32_t)buf[512];
		if (magic == 0x686a6673 || magic == 0x73666a68) {
			*name = "hjfs";
			return 1;
		}
		if (magic == 0x67656673 || magic == 0x73666567) {
			*name = "gefs";
			return 1;
		}
	}
	return 0;
}

static int
do_blkid(const char *path)
{
	struct BlockDev dev;
	unsigned char *buf;
	size_t read_size = 262144; /* 256kb */
	size_t i, j;
	int found = 0;

	if (blockdev_open(&dev, path, 0) < 0)
		return -1;

	if (dev.size < read_size)
		read_size = dev.size;

	buf = emalloc(read_size);
	/* read sectors */
	size_t sectors_to_read = (read_size + dev.sec_size - 1) / dev.sec_size;
	if (blockdev_read(&dev, 0, buf, sectors_to_read) < 0) {
		free(buf);
		blockdev_close(&dev);
		return -1;
	}

	for (i = 0; i < LEN(fstypes); i++) {
		const struct FsType *fs = &fstypes[i];
		if (fs->magic_offset + fs->magic_len > read_size)
			continue;

		uint64_t val = 0;
		for (j = 0; j < fs->magic_len; j++) {
			val |= ((uint64_t)buf[fs->magic_offset + j]) << (8 * j);
		}

		if (val == fs->magic1 || (fs->magic2 && val == fs->magic2)) {
			const char *type_name = fs->name;
			if (strcmp(fs->name, "ext") == 0) {
				if (buf[1116] & 4)
					type_name = "ext3";
				else if (buf[1120] & 64)
					type_name = "ext4";
				else
					type_name = "ext2";
			}

			if (Uflag || Lflag) {
				char label[256] = {0};
				char uuid[256] = {0};
				if (fs->label_len && fs->label_offset + fs->label_len <= read_size) {
					snprintf(label, sizeof(label), "%.*s", (int)fs->label_len, buf + fs->label_offset);
				}
				if (fs->uuid_len && fs->uuid_offset + fs->uuid_len <= read_size) {
					format_uuid(buf + fs->uuid_offset, fs->uuid_len, fs->name, uuid, sizeof(uuid));
				}
				if (Uflag) {
					printf("%s\n", uuid);
				} else {
					printf("%s\n", label);
				}
				found = 1;
				break;
			}

			if (strcmp(oflag, "export") == 0) {
				printf("DEVNAME=%s\n", path);
			} else if (strcmp(oflag, "value") != 0) {
				printf("%s:", path);
			}

			if (fs->label_len && fs->label_offset + fs->label_len <= read_size) {
				char label[256];
				snprintf(label, sizeof(label), "%.*s", (int)fs->label_len, buf + fs->label_offset);
				/* strip trailing spaces */
				char *end = label + strlen(label) - 1;
				while (end >= label && *end == ' ') {
					*end = '\0';
					end--;
				}
				if (label[0])
					print_tag(path, "LABEL", label);
			}

			if (fs->uuid_len && fs->uuid_offset + fs->uuid_len <= read_size) {
				char uuid[256];
				format_uuid(buf + fs->uuid_offset, fs->uuid_len, fs->name, uuid, sizeof(uuid));
				print_tag(path, "UUID", uuid);
			}

			print_tag(path, "TYPE", type_name);
			if (strcmp(oflag, "full") == 0)
				printf("\n");

			found = 1;
			break;
		}
	}

	if (!found && detect_zfs(buf, read_size)) {
		if (Uflag || Lflag) {
			printf("\n");
		} else {
			if (strcmp(oflag, "export") == 0) {
				printf("DEVNAME=%s\n", path);
			} else if (strcmp(oflag, "value") != 0) {
				printf("%s:", path);
			}
			print_tag(path, "TYPE", "zfs_member");
			if (strcmp(oflag, "full") == 0)
				printf("\n");
		}
		found = 1;
	}

	if (!found) {
		const char *p9_name = NULL;
		if (detect_plan9_other(buf, read_size, &p9_name)) {
			if (Uflag || Lflag) {
				printf("\n");
			} else {
				if (strcmp(oflag, "export") == 0) {
					printf("DEVNAME=%s\n", path);
				} else if (strcmp(oflag, "value") != 0) {
					printf("%s:", path);
				}
				print_tag(path, "TYPE", p9_name);
				if (strcmp(oflag, "full") == 0)
					printf("\n");
			}
			found = 1;
		}
	}

	free(buf);
	blockdev_close(&dev);
	return found ? 0 : -2;
}

// ?man blkid: print block device attributes
// ?man arguments: [device ...]
// ?man blkid locates and prints attributes (such as uuid, volume label, and filesystem type)
// ?man of block devices or partition images
int
main(int argc, char *argv[])
{
	int ret = 0;

	ARGBEGIN {
	// ?man -o:specify output format (full, value, export)
	case 'o':
		oflag = EARGF(usage());
		break;
	// ?man -s:only show specified tag (e.g. UUID, LABEL, TYPE)
	case 's':
		sflag = EARGF(usage());
		break;
	// ?man -U:print UUID only
	case 'U':
		Uflag = 1;
		break;
	// ?man -L:print volume label only
	case 'L':
		Lflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc > 0) {
		for (; *argv; argv++) {
			if (do_blkid(*argv) < 0)
				ret = 1;
		}
	} else {
		struct BlockDevInfo *list = blockdev_get_list();
		struct BlockDevInfo *curr;
		if (!list) {
			return 1;
		}
		for (curr = list; curr; curr = curr->next) {
			char devpath[128];
			snprintf(devpath, sizeof(devpath), "/dev/%s", curr->name);
			do_blkid(devpath);
			struct BlockDevInfo *part;
			for (part = curr->parts; part; part = part->next) {
				snprintf(devpath, sizeof(devpath), "/dev/%s", part->name);
				do_blkid(devpath);
			}
		}
		blockdev_free_list(list);
	}

	return ret;
}
