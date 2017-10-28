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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <ftw.h>
#include <search.h>
#include <iostream>
#include <map>
#include <string>

using namespace std;

#define FTW_OPEN_FD			2000

#define DEVICE_NAME		"mmcblk0p28"
#define PART_NAME		"/data"

#define DIRNAME	0
#define FILENAME	1

#define TYPE_DEFAULT	1
#define TYPE_FSYNC	2	
#define TYPE_RENAME	3
#define TYPE_WRITE	4
#define TYPE_OPEN	5

#define OUTPUT_NAME(BUF, PATH, PNAME) memset(BUF, 0, PATH_MAX + 8);	sprintf(BUF, "%s_%s.input", PATH, PNAME);


struct Name *arrName_p;
struct Name *arrName_p_s[20];
int s_count = 0;
char name_p[PATH_MAX + 1];
int t_index;
struct Namemap_p *entry_p = 0;

struct Name
{
	char name[PATH_MAX + 1];
	double delete_time;
};

struct Namemap
{
	long long int inode;
	int count;
	struct Name arrName[20];
};

static char lost_found_dir[PATH_MAX + 1];

int mkdir_all_path(const char *path);
static int convert_trace(const char *file, const struct stat *buf, 
		int flag, struct FTW *ftwbuf);
static int preopened_files(char *file, map<int, struct Namemap*> *name_map);
static int construct_name_map(char *file, map<int, struct Namemap*> *name_map);
static int print_trace(char *file, map<int, struct Namemap*> *name_map, 
							map<int, int> *pid_map, map<int, string> *ppid_map);
int compare_inode(const void *node1, const void *node2);
int namemap_insert(struct Namemap* entry, char *path);
struct Namemap* make_namemap_entry(long long int inode);
void free_entry(void *entry);
int compare_user(const void *node1, const void *node2);
int namemap_insert_p(struct Namemap_p* entry, char *path);
struct Namemap_p* make_namemap_entry_p(char* name);
void free_entry_p(void *entry);

double parse_time(char* line);
int parse_pid(char* line);

void print_help()
{
	printf("traceReplay 0.2 Version\n");
	printf("OPEN\n");
}

int main (int argc, char *argv[])
{
	char dir_name[PATH_MAX + 1];
	int opt;
	int i;
	int arg_type = -1;
	struct stat buf;
	char open_file_name[PATH_MAX + 1];
	FILE *open_file_fp;
	int flags = FTW_PHYS | FTW_MOUNT;
	int mount_dir_len;
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
			printf("Failed: %s\n", argv[i]);
		}

		if (S_ISLNK(buf.st_mode)) {
			struct stat buf2;
			if (stat(argv[i], &buf2) == 0 &&
					S_ISBLK(buf2.st_mode))
				buf = buf2;
		}
		if (S_ISBLK(buf.st_mode)) {
			printf("Failed: %s\n", argv[i]);
			continue;
		} else if (S_ISDIR(buf.st_mode)) {
			if (access(argv[i], R_OK) < 0) {
				continue;
			}
			arg_type = DIRNAME;
			strncpy(dir_name, argv[i], strnlen(argv[i], PATH_MAX));
		} else if (S_ISREG(buf.st_mode)) {
			arg_type = FILENAME;
		} else {
			printf("failed: %s\n", argv[i]);
			continue;
		}

		switch (arg_type) {
			mount_dir_len = 0;
			case DIRNAME:
			{
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
						printf("failed: %s\n", argv[i]);
						continue;
					}
					memset(lost_found_dir, 0, PATH_MAX + 1);
				}
				nftw(dir_name, convert_trace, FTW_OPEN_FD, flags);
				break;
			}
			case FILENAME:
			{
				strncat(lost_found_dir, "/lost+found",
								PATH_MAX - strnlen(lost_found_dir, PATH_MAX));
				if (strncmp(lost_found_dir, dir_name,
				                     strnlen(lost_found_dir,
									   PATH_MAX)) == 0) {
					printf("failed: %s\n", argv[i]);
					continue;
				}
				convert_trace(argv[i], &buf, FTW_OPEN_FD, (struct FTW*)flags);
				break;			
			}
		}
	}

out:
	return 0;

}

static int get_ps(char* file, map<int, int> *pid_map, map<int, string> *ppid_map)
{
	char line[2048];
	int ret;
	FILE *preopen_fp;
	struct Namemap_p *entry = 0;
	char preopen_name[PATH_MAX + 8];
	int p_cnt = 0;

	memset(preopen_name, 0, PATH_MAX + 8);
	sprintf(preopen_name, "%s_PS", file);

	preopen_fp = fopen(preopen_name, "r");
	if (preopen_fp == NULL)
		return -1;

	printf("read ps file:%s\n", preopen_name);

	fgets(line, 2048, preopen_fp);
	while (fgets(line, 2048, preopen_fp) != NULL)
	{
		char *tmp, *ptr;
		char user[PATH_MAX + 1], name[PATH_MAX + 1];
		char *path;
		void *searchMap = 0;
		int user_num = -1;
		int pid;
		char *user_p; 
		int i;

		ptr = strtok(line, " ");
		if (ptr == NULL)
			continue;
		strncpy(user, ptr, strlen(ptr));
		user[strlen(ptr)] = 0x00;

		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;

		pid = atoi(ptr);

		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, " ");
		if (ptr == NULL)
			continue;
	
		if (strstr(user, "u0_a") == NULL)
			user_num = -1;
		else
		{
			user_p = user;
			user_p += 4;
			user_num = atoi(user_p);
		}

		memset(name, 0, PATH_MAX+1);

		if (user_num == -1)
			strncpy(name, "system", strlen("system"));
		else 
			strncpy(name, ptr, strlen(ptr));

		ptr = strtok(name, "\x0d");
		if(ptr != NULL)
			strncpy(name, ptr, strlen(ptr));
		name[strlen(ptr)] = 0x00;
		for(i = 0 ; i < strlen(name); i++)
				if(name[i] == '/')
					name[i] = '.';

		(*pid_map)[pid] = user_num;
		if (ppid_map->find(user_num) == ppid_map->end())
			(*ppid_map)[user_num] = string(name);
	}

	return p_cnt;
}

static int convert_trace(const char *file_p, const struct stat *buf, 
		int flag, struct FTW *ftwbuf)
{
	char file[PATH_MAX + 1];
	int ret = 0;
	map<int, struct Namemap*> name_map;
	map<int, int> pid_map;
	map<int, string> ppid_map;

	char *tmp;
	int p_cnt = 0, i;

	strcpy(file, file_p);

	if(strstr(file, ".input") != NULL)
		return 0;
	if((tmp = strstr(file, "TRACE_")) == NULL)
		return 0;
	if((strstr(tmp, "_PREOPEN")) != NULL)
		return 0;
	if((strstr(tmp, "_PS")) != NULL)
		return 0;

    if (lost_found_dir[0] != '\0' &&
			!memcmp(file, lost_found_dir, strnlen(lost_found_dir, PATH_MAX))) {
		return 0;
	}

	if (!S_ISREG(buf->st_mode)) {
		return 0;
	}

	p_cnt = get_ps(file, &pid_map, &ppid_map);
	preopened_files(file, &name_map);

	ret = construct_name_map(file, &name_map);
	if (ret < 0) {
		printf("Error: construct_name_map\n");
		return 0;
	}

	print_trace(file, &name_map, &pid_map, &ppid_map);

	return 0;
}

static int construct_name_map(char *file, map<int, struct Namemap*> *namemap)
{
	char line[2048];
	int line_count = 0;
	int ret;
	FILE *trace_fp, *output_fp;
	int first_trace = 0;
	double start_time;
	int valid_trace = 0;

	trace_fp = fopen(file, "r");
	if (trace_fp == NULL) {
		
		printf("Error: can not read %s\n", file);
		return -1;
	}

	while (fgets(line, 2048, trace_fp) != NULL)
	{
		char *tmp, *ptr;
		char path[PATH_MAX + 1];
		double time;
		char type[10];
		int format_type = -1;
		long long int inode_num;

		line_count = line_count + 1;

		if(strstr(line, DEVICE_NAME) == NULL)
			continue;

		time = parse_time(line);

		if (time == -1)
			continue;

		if (first_trace == 0) {
			start_time = time;
			first_trace = 1;
		}
		time = time - start_time;

		if ((tmp = strstr(line, "[CR]")) != NULL) 
			format_type = TYPE_DEFAULT;	
		else if ((tmp = strstr(line, "[MD]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[UN]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[RD]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[FS]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[RN]")) != NULL)
			format_type = TYPE_RENAME;
		else if ((tmp = strstr(line, "[OP]")) != NULL)
			format_type = TYPE_OPEN;
		else 
			continue;
		valid_trace++;

		strncpy(type, tmp, 4);
		type[4] = 0x00;

		ptr = strtok(tmp, "\t");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, "\t");
		if (ptr == NULL)
			continue;

		if (format_type == TYPE_DEFAULT || format_type == TYPE_OPEN) 
		{
			struct Namemap *entry;
			void *searchMap;
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			if (format_type == TYPE_OPEN) {
				ptr += 5;
			}
			strncpy(path, ptr, strlen(ptr));
			path[strlen(ptr)] = 0x00;
			ptr = strtok(NULL, "\t");
			if (ptr == NULL) {
				continue;
			}
			inode_num = atoll(ptr);
		
			if(namemap->find(inode_num) == namemap->end())
			{
				entry = make_namemap_entry(inode_num);
				namemap_insert(entry, path);
				if (strstr(line, "[UN]") != NULL)
					entry->arrName[0].delete_time = time;
				(*namemap)[inode_num] = entry;
			}
			else if (strstr(line, "[CR]") != NULL)
			{
				entry = (*namemap)[inode_num];
				int count = entry->count - 1;
				if (entry->arrName[count].delete_time == 0)
					entry->arrName[count].delete_time = time;
				namemap_insert(entry, path);
			}
			else if (strstr(line, "[UN]") != NULL)
			{
				entry = (*namemap)[inode_num];
				int count = entry->count - 1;
				if (entry->arrName[count].delete_time == 0)
					entry->arrName[count].delete_time = time;
			}
			else if (format_type != TYPE_OPEN)
			{
				entry = (*namemap)[inode_num];
				int count = entry->count - 1;
				memset(entry->arrName[count].name, 0, PATH_MAX + 1);
				strncpy(entry->arrName[count].name, path, strlen(path));
				entry->arrName[count].name[strlen(path)] = 0x00;
			}

		}
		else if (format_type == TYPE_RENAME)
		{
			char output_name[PATH_MAX + 8];
			char path2[PATH_MAX + 1];
			struct Namemap *entry;
			void *searchMap;

			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			strncpy(path, ptr, strlen(ptr));
			path[strlen(ptr)] = 0x00;

			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			strncpy(path2, ptr, strlen(ptr));
			path2[strlen(ptr)] = 0x00;

			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			inode_num = atoll(ptr);

			if(namemap->find(inode_num) == namemap->end())
			{
				entry = make_namemap_entry(inode_num);
				namemap_insert(entry, path);
				if (strstr(line, "[UN]") != NULL)
					entry->arrName[0].delete_time = time;
				(*namemap)[inode_num] = entry;
			}
			else
			{
				entry = (*namemap)[inode_num];
				int count = entry->count - 1;
				if (entry->arrName[count].delete_time == 0)
					entry->arrName[count].delete_time = time;
				namemap_insert(entry, path2);
			}

		}
		else
			continue;
	}

	fclose(trace_fp);

	if (valid_trace == 0) {
		printf("Error: valid_trace = 0 %s\n", file);
		return -1;
	}

	return 0;
}


int compare_inode(const void *node1, const void *node2)
{
	const struct Namemap *namemap1 = (const struct Namemap*) node1;
	const struct Namemap *namemap2 = (const struct Namemap*) node2;

	if (namemap1->inode > namemap2->inode)
		return 1;
	else if (namemap1->inode < namemap2->inode)
		return -1;
	else
		return 0;
}

static int preopened_files(char *file, map<int, struct Namemap*> *namemap)
{
	char line[2048];
	int line_count = 0;
	int ret;
	FILE *preopen_fp, *output_fp;
	struct Namemap *entry = 0;
	void *searchMap = 0;
	char preopen_name[PATH_MAX + 8];

	memset(preopen_name, 0, PATH_MAX + 8);
	sprintf(preopen_name, "%s_PREOPEN", file);

	preopen_fp = fopen(preopen_name, "r");
	if (preopen_fp == NULL)
		return -1;

	printf("read preopen file:%s\n", preopen_name);

	while (fgets(line, 2048, preopen_fp) != NULL)
	{
		char *tmp, *ptr;
		char full_path[PATH_MAX + 1];
		char *path;
		long long int inode_num = -1;
		struct Namemap* entry;

		line_count = line_count + 1;

		if (line[0] != '/')
			continue;
		if (line[1] != 'd')
			continue;
		if (line[2] != 'a')
			continue;
		if (line[3] != 't')
			continue;
		if (line[4] != 'a')
			continue;

		ptr = strtok(line, "\t");
		if (ptr == NULL)
			continue;
		strncpy(full_path, ptr, strlen(ptr));
		full_path[strlen(ptr)] = 0x00;
		path = full_path + 5;

		ptr = strtok(NULL, "\t");
		if (ptr == NULL)
			continue;
		inode_num = atoll(ptr);

		if(namemap->find(inode_num) == namemap->end())
		{
			entry = make_namemap_entry(inode_num);
			namemap_insert(entry, path);
			namemap->insert(pair<int, struct Namemap*>(inode_num, entry));
		}
		else
		{
			entry = namemap->find(inode_num)->second;
			namemap_insert(entry, path);
		}
	}

	fclose(preopen_fp);
}
static int print_trace(char *file, map<int, struct Namemap*> *namemap, 
				map<int, int> *pid_map, map<int, string> *ppid_map)
{
	char line[2048];
	char user[PATH_MAX + 1];
	int line_count = 0;
	int ret;
	FILE *trace_fp, *output_fp;
	int first_trace = 0;
	double start_time;
	int sz = 0;
	char mname[PATH_MAX + 1];
	int i;

	trace_fp = fopen(file, "r");
	if (trace_fp == NULL)
		return 0;

	if (trace_fp == NULL)
		return 0;
	while (fgets(line, 2048, trace_fp) != NULL)
	{
		char *tmp, *ptr;
		char output_name[PATH_MAX + 8];
		char path[PATH_MAX + 1];
		double time;
		char type[10];
		int format_type = -1;
		long long int inode_num;
		struct Namemap *entry = 0;
		void *searchMap = 0;
		int pid;
		int uid;

		line_count = line_count + 1;

		if(strstr(line, DEVICE_NAME) == NULL)
			continue;
	
		pid = parse_pid(line);
		time = parse_time(line);

		if (time == -1)
			continue;

		if (first_trace == 0)
		{
			start_time = time;
			first_trace = 1;
		}

		time = time - start_time;

		if ((tmp = strstr(line, "[CR]")) != NULL) 
			format_type = TYPE_DEFAULT;	
		else if ((tmp = strstr(line, "[MD]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[UN]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[RD]")) != NULL)
			format_type = TYPE_DEFAULT;
		else if ((tmp = strstr(line, "[W]")) != NULL)
			format_type = TYPE_WRITE;
		else if ((tmp = strstr(line, "[FS]")) != NULL)
			format_type = TYPE_FSYNC;
		else if ((tmp = strstr(line, "[RN]")) != NULL)
			format_type = TYPE_RENAME;
		else 
			continue;

		if (format_type == TYPE_WRITE)
		{
			strncpy(type, tmp, 3);
			type[3] = 0x00;
		} else {
			strncpy(type, tmp, 4);
			type[4] = 0x00;
		}

		ptr = strtok(tmp, "\t");
		if (ptr == NULL)
			continue;
		ptr = strtok(NULL, "\t");
		if (ptr == NULL)
			continue;

		if (format_type == TYPE_DEFAULT) 
		{
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			strncpy(path, ptr, strlen(ptr));
			path[strlen(ptr)] = 0x00;
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			inode_num = atoll(ptr);	
			if(pid_map->find(pid) == pid_map->end())
			{
				OUTPUT_NAME(output_name, file, "");
			}
			else
			{
				uid = pid_map->find(pid)->second;
				OUTPUT_NAME(output_name, file, ppid_map->find(uid)->second.c_str());
			}
			output_fp = fopen(output_name, "a+");
			fprintf(output_fp, "%lf\t%s\t%s\t\n", time, type, path);
			fclose(output_fp);
		}
		else if (format_type == TYPE_FSYNC)
		{
				int sync_option;
				ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			strncpy(path, ptr, strlen(ptr));
			path[strlen(ptr)] = 0x00;
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			inode_num = atoll(ptr);

			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
					continue;
			sync_option = atoi(ptr);
			if(pid_map->find(pid) == pid_map->end())
			{
				OUTPUT_NAME(output_name, file, "");
			}
			else
			{
				uid = pid_map->find(pid)->second;
				OUTPUT_NAME(output_name, file, ppid_map->find(uid)->second.c_str());
			}
			output_fp = fopen(output_name, "a+");
			fprintf(output_fp, "%lf\t%s\t%s\t%d\t\n", time, type, path, sync_option);
			fclose(output_fp);
		}
		else if (format_type == TYPE_RENAME)
		{
				char path2[PATH_MAX + 1];

			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			strncpy(path, ptr, strlen(ptr));
			path[strlen(ptr)] = 0x00;
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			strncpy(path2, ptr, strlen(ptr));
			path2[strlen(ptr)] = 0x00;
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
					continue;
			inode_num = atoll(ptr);
			if(pid_map->find(pid) == pid_map->end())
			{
				OUTPUT_NAME(output_name, file, "");
			}
			else
			{
				uid = pid_map->find(pid)->second;
				OUTPUT_NAME(output_name, file, ppid_map->find(uid)->second.c_str());
			}
			output_fp = fopen(output_name, "a+");
			fprintf(output_fp, "%lf\t%s\t%s\t%s\t\n", time, type, path, path2);
			fclose(output_fp);
		}
		else if (format_type == TYPE_WRITE)
		{
				long long int write_off;
			long long int  write_size;
			long long int file_size;

			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			inode_num = atoll(ptr);
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			write_off = atoll(ptr);
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			write_size = atoll(ptr);
			ptr = strtok(NULL, "\t");
			if (ptr == NULL)
				continue;
			file_size = atoll(ptr);


			if(namemap->find(inode_num) == namemap->end())
			{
				char unknown_name[PATH_MAX + 1];
				sprintf(unknown_name, "unknown_%lld", inode_num);
				strncpy(path, unknown_name, strlen(unknown_name));
				path[strlen(unknown_name)] = 0x00;
			}
			else
			{
					int max_count, cnt;
					entry = namemap->find(inode_num)->second;
					max_count = entry->count;

					for (cnt = 0; cnt < max_count; cnt++)
					{
							if (entry->arrName[cnt].delete_time == 0)
									break;
							if (time < entry->arrName[cnt].delete_time)
									break;
					}
					if (cnt == max_count)
							cnt--;

					memset(path, 0, PATH_MAX + 1);
					strncpy(path, entry->arrName[cnt].name, strlen(entry->arrName[cnt].name));
					path[strlen(entry->arrName[cnt].name)] = 0x00;		
			}
			if(pid_map->find(pid) == pid_map->end())
			{
				OUTPUT_NAME(output_name, file, "");
			}
			else
			{
				uid = pid_map->find(pid)->second;
				OUTPUT_NAME(output_name, file, ppid_map->find(uid)->second.c_str());
			}
			output_fp = fopen(output_name, "a+");

			if (write_off == file_size)
					fprintf(output_fp, "%lf\t%s\t%s\t%llu\t%llu\t%llu\t\n", time, "[WA]", path, write_off, write_size, file_size);
			else
					fprintf(output_fp, "%lf\t%s\t%s\t%llu\t%llu\t%llu\t\n", time, "[WO]", path, write_off, write_size, file_size);
			fclose(output_fp);
		}
		else
				continue;
	}
	fclose(trace_fp);
	return 0;
}

struct Namemap* make_namemap_entry(long long int inode)
{
	struct Namemap* new_namemap;
	new_namemap = (struct Namemap*) malloc(sizeof(struct Namemap));
	new_namemap->inode = inode;
	new_namemap->count = 0;
	return new_namemap;
}

int namemap_insert(struct Namemap* entry, char *path)
{
	int count = entry->count;
	if (count >= 20) {
		// printf("warning: %lld %s\n", entry->inode, path);
		return -1;
	}
	memset(entry->arrName[count].name, 0, PATH_MAX + 1);
	strncpy(entry->arrName[count].name, path, strlen(path));
	entry->arrName[count].name[strlen(path)] = 0x00;
	entry->arrName[count].delete_time = 0;
	entry->count = entry->count + 1;
}

void free_entry(void *entry)
{
	struct Namemap *free_entry = (struct Namemap*)entry;
	if (free_entry == NULL)
		return;
	free(entry);
	entry = NULL;
}
double parse_time(char* line)
{
	char* tmp = line;
	char time_str[20];
	char *ptr;
	double ret;
	if (strlen(line) < 47)
		return -1;
	tmp+= 33;					// ftrace format
	strncpy(time_str, tmp, 13);

	ret = strtod(time_str, &ptr);

	if (ret <= 0)
		return -1;

	return ret;
}
int parse_pid(char* line)
{
	char* tmp = line;
	char time_str[20];
	char *ptr;
	int ret;
	if (strlen(line) < 47)
		return -1;
	tmp+= 17;					// ftrace format
	strncpy(time_str, tmp, 5);

	ret = atoll(time_str);

	if (ret <= 0)
		return -1;

	return ret;
}



