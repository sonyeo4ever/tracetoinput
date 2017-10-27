#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef __USE_LARGEFILE64
#define __USE_LARGEFILE64
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <stdio.h>
#include <dirent.h>
#include <endian.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ftw.h>
#include <mntent.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/fiemap.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/vfs.h>

#define EXTENT_MAX_COUNT    512
#define EXT4_IOC_MOVE_EXT               _IOWR('f', 15, struct move_extent)
#define FTW_OPEN_FD     2000

#define PRINT_FILE_NAME(file)   printf("\"%s\"\n", (file))

#define DEVNAME	0
#define DIRNAME	1
#define FILENAME	2

static char lost_found_dir[PATH_MAX + 1];

static int print_file_stat(char *file, const struct stat64 *buf, int flag, struct FTW *ftwbuf);

void print_help()
{
  printf("Usage: lookFile [options] FileName/Directory\n");
	printf("\n");
	printf("lookAllFile version 0.1\n");
	printf("\n");
	printf("options:\n");
  printf("      -h: Print Usage()\n");
//    printf("      -D: Defragmentation (ignore options \"s\")\n");
}

int main(int argc, char *argv[])
{
	char dir_name[PATH_MAX + 1];
	struct stat64 buf;
	int flags = FTW_PHYS | FTW_MOUNT;
	int i;
	int arg_type = -1;
	int opt;
	int temp_size;

	if (argc == 1) {
		print_help();
	   return 0;
    }

	while ((opt = getopt(argc, argv, "h")) != EOF) {
	  switch (opt) {
	  	case 'h':
			print_help();
			goto out;
		default:
			print_help();
			goto out;
	  }
	}

	for (i = optind; i < argc; i++) {

		memset(dir_name, 0, PATH_MAX + 1);

		if (lstat(argv[i], &buf) < 0) {
			printf("Failed: ");
			PRINT_FILE_NAME(argv[i]);
			goto out;
		}

		if (S_ISLNK(buf.st_mode)) {
			struct stat64 buf2;
			if (stat64(argv[i], &buf2) == 0 &&
					S_ISBLK(buf2.st_mode))
				buf = buf2;
		}

		if (S_ISBLK(buf.st_mode)) {
			printf("Failed: ");
			PRINT_FILE_NAME(argv[i]);
			continue;
      /* Block device */
/*			strncpy(dev_name, argv[i], strnlen(argv[i], PATH_MAX));
			if (get_mount_point(argv[i], dir_name, PATH_MAX) < 0)
				continue;
				if (lstat64(dir_name, &buf) < 0) {
					PRINT_FILE_NAME(argv[i]);
					continue;
				}
			arg_type = DEVNAME;
			if (!(mode_flag & STATISTIC))
				printf("ext4 defragmentation for device(%s)\n",
					argv[i]);
					*/
		} else if (S_ISDIR(buf.st_mode)) {
             /* Directory */
			if (access(argv[i], R_OK) < 0) {
                 continue;
			}
			arg_type = DIRNAME;
			strncpy(dir_name, argv[i], strnlen(argv[i], PATH_MAX));
		} else if (S_ISREG(buf.st_mode)) {
			/* Regular file */
			arg_type = FILENAME;
		} else {
			/* Irregular file */
			printf("Failed: ");
			PRINT_FILE_NAME(argv[i]);
			continue;
		}

		if (arg_type == FILENAME || arg_type == DIRNAME) {

		//	if (is_ext4(argv[i], dev_name) < 0 )
		//		continue;
			if (realpath (argv[i], dir_name) == NULL) {
				printf("Failed: ");
				PRINT_FILE_NAME(argv[i]);
				continue;
			}
		}

		switch (arg_type) {
			int mount_dir_len = 0;
			case DIRNAME:
				printf("[%s]\n", realpath(argv[i], dir_name));
				mount_dir_len = strnlen(lost_found_dir, PATH_MAX);
				strncat(lost_found_dir, "/lost+found",
								PATH_MAX - strnlen(lost_found_dir, PATH_MAX));
				if (dir_name[mount_dir_len] != '\0') {

					if (strncmp(lost_found_dir, dir_name,
						strnlen(lost_found_dir,
						      PATH_MAX)) == 0 &&
						(dir_name[strnlen(lost_found_dir,
						      PATH_MAX)] == '\0' ||
						dir_name[strnlen(lost_found_dir,
						      PATH_MAX)] == '/')) {
						PRINT_FILE_NAME(argv[i]);
						continue;
						}

					memset(lost_found_dir, 0, PATH_MAX + 1);
				}
			case DEVNAME:
				if (arg_type == DEVNAME) {
					// TODO
					continue;
				}
				nftw64(dir_name, print_file_stat, FTW_OPEN_FD, flags);
				break;

			case FILENAME:
				strncat(lost_found_dir, "/lost+found",
								PATH_MAX - strnlen(lost_found_dir, PATH_MAX));
				if (strncmp(lost_found_dir, dir_name,
				                     strnlen(lost_found_dir,
									   PATH_MAX)) == 0) {
					PRINT_FILE_NAME(argv[i]);
					continue;
				}

				print_file_stat(argv[i], &buf, FTW_OPEN_FD, flags);
				break;
		}
	}

out:

	return 0;
}

static int print_file_stat(char *file, const struct stat64 *buf,
		int flag, struct FTW *ftwbuf)
{
	int fd = -1;
	struct stat file_stat;
	int ret;
	unsigned long inode;
	unsigned long filesize;

	if (lost_found_dir[0] != '\0' &&
	         !memcmp(file, lost_found_dir, strnlen(lost_found_dir, PATH_MAX))) {
		return 0;
	}

	if (!S_ISREG(buf->st_mode)) {
		return 0;
	}

	if (buf->st_size == 0) {
		return 0;
	}

	if (buf->st_blocks == 0) {
		return 0;
	}

	fd = open64(file, O_RDWR);
	if (fd < 0) {
	//	printf("Fail create file: %s\n", file);
		return 0;
	}

	ret = fstat(fd, &file_stat);
	if (ret < 0)
	{
		printf("%s: fstat error\n", file);
	}
	inode = file_stat.st_ino;
	filesize = file_stat.st_size;
	printf("%s\t%lu\t%lu\n", file, inode, filesize);

	close(fd);

	return 0;
}

