/* See LICENSE file for copyright and license details. */
#include "../diskutil.h"
#include "../util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(__linux__)
#include <sys/ioctl.h>
#include <linux/fs.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__Apple__)
#include <sys/ioctl.h>
#include <sys/disk.h>
#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/dkio.h>
#include <sys/disklabel.h>
#include <sys/sysctl.h>
#elif defined(__FreeBSD__)
#include <sys/disk.h>
#include <sys/sysctl.h>
#include <dirent.h>
#endif
#endif

int
blockdev_open(struct BlockDev *dev, const char *path, int writable)
{
	struct stat st;
	int fd, flags;

	flags = writable ? O_RDWR : O_RDONLY;
	fd = open(path, flags);
	if (fd < 0)
		return -1;

	if (fstat(fd, &st) < 0) {
		close(fd);
		return -1;
	}

	dev->fd = fd;
	dev->size = st.st_size;
	dev->sec_size = 512;

	if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
#if defined(__linux__)
		uint64_t size_bytes = 0;
		int sec_size = 0;
		if (ioctl(fd, BLKGETSIZE64, &size_bytes) >= 0)
			dev->size = size_bytes;
		if (ioctl(fd, BLKSSZGET, &sec_size) >= 0)
			dev->sec_size = sec_size;
#elif defined(__FreeBSD__)
		off_t size_bytes = 0;
		unsigned int sec_size = 0;
		if (ioctl(fd, DIOCGMEDIASIZE, &size_bytes) >= 0)
			dev->size = size_bytes;
		if (ioctl(fd, DIOCGSECTORSIZE, &sec_size) >= 0)
			dev->sec_size = sec_size;
#elif defined(__OpenBSD__) || defined(__NetBSD__)
		struct disklabel dl;
		if (ioctl(fd, DIOCGDINFO, &dl) >= 0) {
			dev->size = (uint64_t)dl.d_secsize * DL_GETDSIZE(&dl);
			dev->sec_size = dl.d_secsize;
		}
#endif
	}

	return 0;
}

int
blockdev_read(struct BlockDev *dev, uint64_t sector, void *buf, size_t sector_count)
{
	off_t offset = (off_t)sector * dev->sec_size;
	size_t size = sector_count * dev->sec_size;
	ssize_t n;

	if (lseek(dev->fd, offset, SEEK_SET) == (off_t)-1)
		return -1;

	n = read(dev->fd, buf, size);
	if (n < 0 || (size_t)n != size)
		return -1;

	return 0;
}

int
blockdev_write(struct BlockDev *dev, uint64_t sector, const void *buf, size_t sector_count)
{
	off_t offset = (off_t)sector * dev->sec_size;
	size_t size = sector_count * dev->sec_size;
	ssize_t n;

	if (lseek(dev->fd, offset, SEEK_SET) == (off_t)-1)
		return -1;

	n = write(dev->fd, buf, size);
	if (n < 0 || (size_t)n != size)
		return -1;

	return 0;
}

void
blockdev_close(struct BlockDev *dev)
{
	if (dev->fd >= 0) {
		close(dev->fd);
		dev->fd = -1;
	}
}

struct BlockDevInfo *
blockdev_get_list(void)
{
#if defined(__linux__)
	FILE *fp;
	char line[256];
	struct BlockDevInfo *head = NULL, *tail = NULL;
	unsigned int major, minor;
	unsigned long long blocks;
	char name[64];

	fp = fopen("/proc/partitions", "r");
	if (!fp)
		return NULL;

	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, " %u %u %llu %63s", &major, &minor, &blocks, name) != 4)
			continue;

		struct BlockDevInfo *info = calloc(1, sizeof(*info));
		if (!info)
			break;

		strlcpy(info->name, name, sizeof(info->name));
		info->size = blocks * 1024;
		info->major = major;
		info->minor = minor;

		/* check if this is a partition of an existing disk */
		struct BlockDevInfo *curr;
		int is_part = 0;
		for (curr = head; curr; curr = curr->next) {
			size_t len = strlen(curr->name);
			if (strncmp(name, curr->name, len) == 0) {
				char c = name[len];
				if ((c >= '0' && c <= '9') || (c == 'p' && name[len+1] >= '0' && name[len+1] <= '9')) {
					is_part = 1;
					strlcpy(info->type, "part", sizeof(info->type));
					struct BlockDevInfo *p = curr->parts;
					if (!p) {
						curr->parts = info;
					} else {
						while (p->next)
							p = p->next;
						p->next = info;
					}
					break;
				}
			}
		}

		if (!is_part) {
			strlcpy(info->type, "disk", sizeof(info->type));
			if (!head) {
				head = info;
			} else {
				tail->next = info;
			}
			tail = info;
		}
	}
	fclose(fp);

	/* parse mounts to populate mountpoint and fstype */
	FILE *mfile = fopen("/proc/mounts", "r");
	if (mfile) {
		char mline[512];
		char devpath[128], mnt[256], fs[64];
		while (fgets(mline, sizeof(mline), mfile)) {
			if (sscanf(mline, "%127s %255s %63s", devpath, mnt, fs) == 3) {
				char *devname = devpath;
				if (strncmp(devpath, "/dev/", 5) == 0)
					devname = devpath + 5;
				struct BlockDevInfo *curr;
				for (curr = head; curr; curr = curr->next) {
					if (strcmp(curr->name, devname) == 0) {
						strlcpy(curr->mountpoint, mnt, sizeof(curr->mountpoint));
						strlcpy(curr->fstype, fs, sizeof(curr->fstype));
					}
					struct BlockDevInfo *part;
					for (part = curr->parts; part; part = part->next) {
						if (strcmp(part->name, devname) == 0) {
							strlcpy(part->mountpoint, mnt, sizeof(part->mountpoint));
							strlcpy(part->fstype, fs, sizeof(part->fstype));
						}
					}
				}
			}
		}
		fclose(mfile);
	}

	return head;

#elif defined(__FreeBSD__)
	char disks[1024];
	size_t len = sizeof(disks);
	struct BlockDevInfo *head = NULL, *tail = NULL;

	if (sysctlbyname("kern.disks", disks, &len, NULL, 0) < 0)
		return NULL;

	char *tok, *ptr = disks;
	while ((tok = strsep(&ptr, " \t")) != NULL) {
		if (!*tok)
			continue;

		struct BlockDevInfo *info = calloc(1, sizeof(*info));
		if (!info)
			break;

		strlcpy(info->name, tok, sizeof(info->name));
		strlcpy(info->type, "disk", sizeof(info->type));

		char devpath[64];
		snprintf(devpath, sizeof(devpath), "/dev/%s", tok);
		struct BlockDev dev;
		if (blockdev_open(&dev, devpath, 0) == 0) {
			info->size = dev.size;
			blockdev_close(&dev);
		}

		DIR *dir = opendir("/dev");
		if (dir) {
			struct dirent *de;
			size_t disk_len = strlen(tok);
			while ((de = readdir(dir)) != NULL) {
				if (strncmp(de->d_name, tok, disk_len) == 0) {
					char c = de->d_name[disk_len];
					if (c == 's' || c == 'p') {
						struct BlockDevInfo *part = calloc(1, sizeof(*part));
						if (part) {
							strlcpy(part->name, de->d_name, sizeof(part->name));
							strlcpy(part->type, "part", sizeof(part->type));
							snprintf(devpath, sizeof(devpath), "/dev/%s", de->d_name);
							if (blockdev_open(&dev, devpath, 0) == 0) {
								part->size = dev.size;
								blockdev_close(&dev);
							}
							struct BlockDevInfo *p = info->parts;
							if (!p) {
								info->parts = part;
							} else {
								while (p->next)
									p = p->next;
								p->next = part;
							}
						}
					}
				}
			}
			closedir(dir);
		}

		if (!head) {
			head = info;
		} else {
			tail->next = info;
		}
		tail = info;
	}

	struct statfs *mntbuf;
	int mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (int i = 0; i < mntsize; i++) {
		char *devpath = mntbuf[i].f_mntfromname;
		char *devname = devpath;
		if (strncmp(devpath, "/dev/", 5) == 0)
			devname = devpath + 5;
		struct BlockDevInfo *curr;
		for (curr = head; curr; curr = curr->next) {
			if (strcmp(curr->name, devname) == 0) {
				strlcpy(curr->mountpoint, mntbuf[i].f_mntonname, sizeof(curr->mountpoint));
				strlcpy(curr->fstype, mntbuf[i].f_fstypename, sizeof(curr->fstype));
			}
			struct BlockDevInfo *part;
			for (part = curr->parts; part; part = part->next) {
				if (strcmp(part->name, devname) == 0) {
					strlcpy(part->mountpoint, mntbuf[i].f_mntonname, sizeof(part->mountpoint));
					strlcpy(part->fstype, mntbuf[i].f_fstypename, sizeof(part->fstype));
				}
			}
		}
	}

	return head;

#elif defined(__OpenBSD__)
	char disknames[1024];
	size_t len = sizeof(disknames);
	int mib[2] = { CTL_HW, HW_DISKNAMES };
	struct BlockDevInfo *head = NULL, *tail = NULL;

	if (sysctl(mib, 2, disknames, &len, NULL, 0) < 0)
		return NULL;

	char *tok, *ptr = disknames;
	while ((tok = strsep(&ptr, ",")) != NULL) {
		char *colon = strchr(tok, ':');
		if (colon)
			*colon = '\0';
		if (!*tok)
			continue;

		struct BlockDevInfo *info = calloc(1, sizeof(*info));
		if (!info)
			break;

		strlcpy(info->name, tok, sizeof(info->name));
		strlcpy(info->type, "disk", sizeof(info->type));

		char devpath[64];
		snprintf(devpath, sizeof(devpath), "/dev/r%sc", tok);
		struct BlockDev dev;
		if (blockdev_open(&dev, devpath, 0) == 0) {
			info->size = dev.size;
			blockdev_close(&dev);
		}

		for (char c = 'a'; c <= 'p'; c++) {
			if (c == 'c')
				continue;
			snprintf(devpath, sizeof(devpath), "/dev/r%s%c", tok, c);
			if (blockdev_open(&dev, devpath, 0) == 0) {
				struct BlockDevInfo *part = calloc(1, sizeof(*part));
				if (part) {
					snprintf(part->name, sizeof(part->name), "%s%c", tok, c);
					strlcpy(part->type, "part", sizeof(part->type));
					part->size = dev.size;
					struct BlockDevInfo *p = info->parts;
					if (!p) {
						info->parts = part;
					} else {
						while (p->next)
							p = p->next;
						p->next = part;
					}
				}
				blockdev_close(&dev);
			}
		}

		if (!head) {
			head = info;
		} else {
			tail->next = info;
		}
		tail = info;
	}

	struct statfs *mntbuf;
	int mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	for (int i = 0; i < mntsize; i++) {
		char *devpath = mntbuf[i].f_mntfromname;
		char *devname = devpath;
		if (strncmp(devpath, "/dev/", 5) == 0)
			devname = devpath + 5;
		struct BlockDevInfo *curr;
		for (curr = head; curr; curr = curr->next) {
			if (strcmp(curr->name, devname) == 0) {
				strlcpy(curr->mountpoint, mntbuf[i].f_mntonname, sizeof(curr->mountpoint));
				strlcpy(curr->fstype, mntbuf[i].f_fstypename, sizeof(curr->fstype));
			}
			struct BlockDevInfo *part;
			for (part = curr->parts; part; part = part->next) {
				if (strcmp(part->name, devname) == 0) {
					strlcpy(part->mountpoint, mntbuf[i].f_mntonname, sizeof(part->mountpoint));
					strlcpy(part->fstype, mntbuf[i].f_fstypename, sizeof(part->fstype));
				}
			}
		}
	}

	return head;

#else
	struct BlockDevInfo *head = NULL, *tail = NULL;
	char devname[16];
	for (char c = 'a'; c <= 'z'; c++) {
		snprintf(devname, sizeof(devname), "sd%c", c);
		char devpath[32];
		snprintf(devpath, sizeof(devpath), "/dev/%s", devname);
		struct BlockDev dev;
		if (blockdev_open(&dev, devpath, 0) == 0) {
			struct BlockDevInfo *info = calloc(1, sizeof(*info));
			if (info) {
				strlcpy(info->name, devname, sizeof(info->name));
				strlcpy(info->type, "disk", sizeof(info->type));
				info->size = dev.size;
				if (!head) {
					head = info;
				} else {
					tail->next = info;
				}
				tail = info;
			}
			blockdev_close(&dev);
		}
	}
	return head;
#endif
}

void
blockdev_free_list(struct BlockDevInfo *list)
{
	struct BlockDevInfo *curr = list;
	while (curr) {
		struct BlockDevInfo *next = curr->next;
		struct BlockDevInfo *part = curr->parts;
		while (part) {
			struct BlockDevInfo *next_part = part->next;
			free(part);
			part = next_part;
		}
		free(curr);
		curr = next;
	}
}
