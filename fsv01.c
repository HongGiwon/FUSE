#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#define NUM_BLOCK 0xFFFE
#define NO_NEXT_BLOCK 0xFFFF
//NO_NEXT_BLOCK은 다음 block이 없는 상황을 나타낸다. (empty block, tree 구조 등에서 사용)
#define ERROR_NO_DIR 0xFFFF
#define MAX_FILE_NUM 3627
#define ROOT 0
#define SIB 12
#define DIB 13
#define TIB 14

//gcc -D_FILE_OFFSET_BITS=64 -o fsv01 fsv01.c -lfuse

typedef unsigned short addr;
typedef unsigned short data;


struct descriptor
{
	struct stat st;
	addr parent; // parent directory의 inode index
	addr next;   // next file or directory의 inode index.
	addr fchild; // 첫 번째 자식의 inode index. 
};

struct sBlock
{
    unsigned iSize;
    unsigned dSize;
    unsigned numUsediBlock;
    unsigned numUseddBlock;
    addr iRoot;
    addr iEmpty;
    addr dEmpty;
    char aTime[25];

};

struct iBlock
{
    struct descriptor desc;
    addr db[15];
    char FileName[74];
};

struct dBlock
{
    data data[128];
};



struct sBlock sb;
struct iBlock inode[NUM_BLOCK] = {0};
struct dBlock dblock[NUM_BLOCK] = {0};

void init_storage()
{
	int i;
	sb.iSize = 256;
	sb.dSize = 256;
	sb.numUsediBlock = 0;
	sb.numUseddBlock = 0;
	sb.iRoot = ROOT;
	time_t timer;

	inode[ROOT].desc.st.st_size = 0;
	inode[ROOT].desc.st.st_nlink = 0;
	inode[ROOT].desc.st.st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH;
	inode[ROOT].desc.fchild = NO_NEXT_BLOCK;
	inode[ROOT].desc.next = NO_NEXT_BLOCK;
	inode[ROOT].desc.parent = NO_NEXT_BLOCK;

	sb.iEmpty = ROOT + 1; // index 0인 inode는 root directory
	sb.dEmpty = ROOT;
	for (i= 0; i< NUM_BLOCK-1; i++)
	{
		inode[i].db[0] = i+1;
		dblock[i].data[0] = i+1;
	}
	inode[NUM_BLOCK-1].db[0] = NO_NEXT_BLOCK;
	dblock[NUM_BLOCK-1].data[0] = NO_NEXT_BLOCK;

	time(&timer);	
	strcpy(sb.aTime, ctime(&timer));
}

void load_storage(FILE * fp)
{
	int i;
	time_t timer;
	fread(&sb, sizeof(sb),1,fp);
	for(i=0; i<NUM_BLOCK; i++)
		fread(&inode[i], sizeof(struct iBlock), 1, fp);

	for(i=0; i<NUM_BLOCK; i++)
		fread(&dblock[i], sizeof(struct dBlock), 1, fp);

	time(&timer);	
	strcpy(sb.aTime, ctime(&timer));
	fclose(fp);
}

addr get_empty_inode()
{
	addr cur = sb.iEmpty;

	if(cur == NO_NEXT_BLOCK)
	{
		printf("ERROR : There is no free block.\n");
		return cur;
	}

	sb.iEmpty = inode[cur].db[0];
	printf("Current empty inode is %d.\n", sb.iEmpty);
	sb.numUsediBlock++;
	return cur;
}

addr get_empty_dblock()
{
	addr cur = sb.dEmpty;

	if(cur == NO_NEXT_BLOCK)
	{
		printf("ERROR : There is no free block.\n");
		return cur;
	}

	sb.dEmpty = dblock[cur].data[0];
	memset(&dblock[cur],0,256);
	printf("Current empty dblock is %d.\n", sb.dEmpty);
	sb.numUseddBlock++;
	return cur;
}
	
void set_empty_inode(addr idx)
{
	addr parent = inode[idx].desc.parent;
	(inode[parent].desc.st.st_nlink)--;
	
	memset(&inode[idx],0,256);
	inode[idx].desc.parent = 0;
	inode[idx].desc.fchild = 0;
	inode[idx].desc.next = NO_NEXT_BLOCK;
	
	inode[idx].db[0] = sb.iEmpty;
	sb.iEmpty = idx;
	printf("Inode[%d] becomes empty.\n", idx);	
	sb.numUsediBlock--;
}

void set_empty_dblock(addr idx)
{
	memset(&dblock[idx],0,256);
	dblock[idx].data[0] = sb.dEmpty;
	sb.dEmpty = idx;
	printf("Dblock[%d] becomes empty.\n", idx);	
	sb.numUseddBlock--;
}

addr get_path(char* path)
{
	char pathtmp[2000];
	char name[74];
	char* ptmp;
	addr tmp = ROOT;
	int count = 0;
	int c= 0, i = 0;
	

	strcpy(pathtmp,path);

	for (i = 0; i < strlen(pathtmp); i++)
	{
		if (pathtmp[i] == '/')
			count++;
	}

	
	ptmp = strtok(pathtmp,"/");
	tmp = inode[tmp].desc.fchild;
	
	//there is no more child node.
	if (tmp == NO_NEXT_BLOCK) return ERROR_NO_DIR;	


	while(c < count)
	{
		strcpy(name,ptmp);
		if (strcmp(name,inode[tmp].FileName) == 0)
		{
			ptmp = strtok(NULL,"/");
			if(ptmp == NULL)
			{
				return tmp;
			}
			tmp = inode[tmp].desc.fchild;
			//there is no more child node.
			if (tmp == NO_NEXT_BLOCK) return ERROR_NO_DIR;
			c++;
		}
	
		else
		{
			tmp = inode[tmp].desc.next;
			//there is no more next node.
			if (tmp == NO_NEXT_BLOCK) return ERROR_NO_DIR;
		}
	}	
	strcpy(name,ptmp);
	
	if (strcmp(name,inode[tmp].FileName) == 0)
		return tmp;
		
	else return ERROR_NO_DIR;
	
	
	return 0;
}

addr get_data_index(addr target, int i)
{//target inode의 i번째 data index 구하는 함수
	int dibtmp, tibtmp;

	if (i< SIB)
		return (inode[target].db[i]);

	else if(i>= SIB && i< (SIB+15))
		return (inode[inode[target].db[SIB]].db[i-SIB]);

	else if (i>=SIB+15 && i< (SIB+15+15*15))
	{
		dibtmp = (i-(SIB+15)) / 15;
		return (inode[inode[inode[target].db[DIB]].db[dibtmp]].db[(i-(SIB+15))-dibtmp*15]);
	}

	else
	{
		tibtmp = (i-(SIB+15)) / (15*15);
		dibtmp = ((i-(SIB+15+15*15))-tibtmp*225)/15;
		return (inode[inode[inode[inode[target].db[TIB]].db[tibtmp]].db[dibtmp]].db[(i-(SIB+15+15*15)-tibtmp*225) % 15]);
	}
}

int put_data_to_index(addr tinode, addr newdblock, int i)
{
	int j, t;
	addr inodetmp = 0, dibtmp = 0, tibtmp = 0;

	if (i< SIB)
		inode[tinode].db[i] = newdblock;

	else if(i == SIB)
	{
		inodetmp = get_empty_inode();
		if ( inodetmp == NO_NEXT_BLOCK )
			return -1;
		inode[tinode].db[SIB] = inodetmp;
		inode[inodetmp].db[0] = newdblock;
	}

	else if(i> SIB && i< (SIB+15))
		inode[inode[tinode].db[SIB]].db[i-SIB] = newdblock;

	else if(i == SIB + 15)
	{
		inodetmp = get_empty_inode();
		if ( inodetmp == NO_NEXT_BLOCK )
			return -1;
		inode[tinode].db[DIB] = inodetmp;
		for (j = 0; j<15; j++)
		{
			dibtmp = get_empty_inode();
			if ( dibtmp == NO_NEXT_BLOCK )
				return -1;
			inode[inodetmp].db[j] = dibtmp;
		}
		inode[inode[inodetmp].db[0]].db[0] = newdblock;
	}

	else if (i>SIB+15 && i< (SIB+15+15*15))
	{
		dibtmp = (i-(SIB+15)) / 15;
		inode[inode[inode[tinode].db[DIB]].db[dibtmp]].db[(i-(SIB+15))-dibtmp*15] = newdblock;
	}

	else if (i == (SIB+15+15*15))
	{
		inodetmp = get_empty_inode();
		if ( inodetmp == NO_NEXT_BLOCK )
			return -1;
		inode[tinode].db[TIB] = inodetmp;
		for (j = 0; j<15; j++)
		{
			dibtmp = get_empty_inode();
			if ( dibtmp == NO_NEXT_BLOCK )
				return -1;
			inode[inodetmp].db[j] = dibtmp;
			for (t = 0; t<15; t++)
			{
				tibtmp = get_empty_inode();
				if ( tibtmp == NO_NEXT_BLOCK )
				return -1;
				inode[dibtmp].db[t] = tibtmp;
			}
		}
		inode[inode[inode[inodetmp].db[0]].db[0]].db[0] = newdblock;
	}

	else
	{
		tibtmp = (i-(SIB+15)) / (15*15);
		dibtmp = ((i-(SIB+15+15*15))-tibtmp*225)/15;
		inode[inode[inode[inode[tinode].db[TIB]].db[tibtmp]].db[dibtmp]].db[(i-(SIB+15+15*15)-tibtmp*225) % 15] = newdblock;
	}
	return 0; // 성공, 제대로 할당 되었음.
}


void insert_dirorfile(addr parent, addr newidx)
{
	addr tmp,tmp2;
	int count = 0, size = 0;
	char* ptmp;
	char filename[74], buf[5];
	
	strcpy(filename, inode[newidx].FileName);
	size = (int)strlen(filename);
	
	if (inode[parent].desc.fchild == NO_NEXT_BLOCK)
		inode[parent].desc.fchild = newidx;
	
	else
	{
		tmp = inode[parent].desc.fchild;
		
		do
		{
			if (strcmp(inode[tmp].FileName, filename) == 0)
			{
				count++;
				sprintf(buf, "%d", count);
				if (size < (int)strlen(filename))
				{
					ptmp = strrchr(filename, '(');
					*(ptmp+1) = (int)buf[0];
				}				
				else
				{
					strcat(filename, "(");
					strcat(filename, buf);
					strcat(filename, ")");
				}
				tmp = inode[parent].desc.fchild;
				continue;
			}
			tmp2 = tmp;
		}while((tmp = inode[tmp].desc.next) != NO_NEXT_BLOCK);
		
		inode[tmp2].desc.next = newidx;
		strcpy(inode[newidx].FileName, filename);
		printf("There are some files which have same name... New file's name has changed.\n");		
	}
	(inode[parent].desc.st.st_nlink)++;
}

void delete_dirorfile(addr delidx){
	addr tmp;
	tmp = inode[delidx].desc.parent;
	tmp = inode[tmp].desc.fchild;

	if(tmp == delidx)
	{
		inode[inode[delidx].desc.parent].desc.fchild = inode[delidx].desc.next;
		return;
	}
	else
	{
		while(1)
		{
			if (inode[tmp].desc.next == delidx)
			{
				inode[tmp].desc.next = inode[delidx].desc.next;
				set_empty_inode(delidx);
				return;
			}
		tmp = inode[tmp].desc.next;
		}
	}
	
	
}

int fuse_mkdir(const char* path, mode_t flag)
{
	char pathtmp[2000], dirname[74];
	char *ptmp;
	struct iBlock dir = {0};
	addr emptyinode;
	addr parentinode;
	

	printf("fuse_mkdir function start,\n");

	emptyinode = get_empty_inode();

	if (emptyinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No free block.\n");
		return -ENOMEM;
	}

	dir.desc.st.st_size = 0;
	dir.desc.st.st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH;
	dir.desc.st.st_nlink = 1;
	dir.desc.st.st_uid = getuid();
	dir.desc.st.st_gid = getgid();
	dir.desc.st.st_atime = time(NULL);
	dir.desc.st.st_mtime = time(NULL);

	strcpy(pathtmp,path);
	ptmp = strrchr(pathtmp,'/');
	if (strlen(ptmp+1) >= 74)
	{
		printf("ERROR : Name of directory is too long.\n");
		return -ENAMETOOLONG;
	}
	strcpy(dirname,ptmp+1);
	strcpy(dir.FileName,dirname);

	pathtmp[ptmp-pathtmp] = '\0';

	if(strcmp(pathtmp,"") == 0)
	{
		parentinode = 0;
	}
	else
	{
		if ((parentinode = get_path(pathtmp)) == ERROR_NO_DIR)
		{
			printf("ERROR : No such directory\n");
			return -ENOENT;
		}
	}

	
	if ((inode[parentinode].desc.st.st_mode & S_IFMT) != S_IFDIR )
	{
		printf("ERROR : Parent is NOT a directory\n");
		return -ENOTDIR;
	}
	dir.desc.parent = parentinode;
	dir.desc.next = NO_NEXT_BLOCK;
	dir.desc.fchild = NO_NEXT_BLOCK;
	inode[emptyinode] = dir;
	insert_dirorfile(parentinode, emptyinode);

	//같은 이름의 폴더-파일 있으면 이름 바꾸기

	printf("fuse_mkdir function complete.\n");

	return 0;

}

int fuse_rmdir(const char* path)
{
	char pathtmp[2000];
	addr tinode;

	printf("rmdir function start.\n");

	strcpy(pathtmp,path);
	tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such directory.\n");
		return -ENOENT;
	}
	if (tinode == ROOT)
	{
		printf("ERROR : Cannot delete root directory.\n");
		return -EPERM;
	}
	if ((inode[tinode].desc.st.st_mode & S_IFMT) != S_IFDIR)
	{
		printf("ERROR : Target is not directory.\n");
		return -ENOTDIR;
	}
	if (inode[tinode].desc.fchild != NO_NEXT_BLOCK)
	{
		printf("ERROR : directory is not empty.\n");
		return -ENOTEMPTY;
	}

	//부모노드 연결 지우기

	delete_dirorfile(tinode);

	printf("rmdir function complete.\n");
	return 0;

}

int fuse_mknod(const char* path, mode_t flag, dev_t dev)
{
	char pathtmp[2000], fname[74];
	char message[1000];
	char *ptmp;
	struct fuse_file_info *info;
	off_t off = 0;
	struct iBlock newfile = {0};
	addr emptyinode;
	addr parentinode;

	printf("fuse_mknod function start.\n");

	emptyinode = get_empty_inode();
	if (emptyinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No free block.\n");
		return -ENOMEM;
	}

	newfile.desc.st.st_size = 0;
	newfile.desc.st.st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IROTH;
	newfile.desc.st.st_nlink = 1;
	newfile.desc.st.st_uid = getuid();
	newfile.desc.st.st_gid = getgid();
	newfile.desc.st.st_atime = time(NULL);
	newfile.desc.st.st_mtime = time(NULL);

	strcpy(pathtmp,path);
	ptmp = strrchr(pathtmp,'/');
	if (strlen(ptmp+1) >= 74)
	{
		printf("ERROR : Name of file is too long.\n");
		return -ENAMETOOLONG;
	}
	strcpy(fname,ptmp+1);
	strcpy(newfile.FileName,fname);

	pathtmp[ptmp-pathtmp] = '\0';
	if(strcmp(pathtmp,"") == 0)
	{
		parentinode = 0;
	}
	else 
	{
		if ((parentinode = get_path(pathtmp)) == ERROR_NO_DIR)
		{
			printf("ERROR : No such directory\n");
			return -ENOENT;
		}
	}
			
	if ((inode[parentinode].desc.st.st_mode & S_IFMT) != S_IFDIR )
	{
		printf("ERROR : Parent is NOT a directory\n");
		return -ENOTDIR;
	}
	newfile.desc.parent = parentinode;
	newfile.desc.next = NO_NEXT_BLOCK;
	newfile.desc.fchild = NO_NEXT_BLOCK;
	newfile.db[0] = NO_NEXT_BLOCK;
	//파일이 생성되었지만 내용은 없음

	inode[emptyinode] = newfile;
	insert_dirorfile(parentinode, emptyinode);

	printf("fuse_mknod function complete.\n");

	strcpy(message,"\nOS project.\nBy 2014313366 Hong Giwon, 2014312406 Sim Hayeong\n\n");

	fuse_write(path, message, sizeof(message),off, info);

	return 0;

}

int fuse_unlink(const char* path)
{
	char pathtmp[2000];
	addr tinode, tdata;
	int i;

	printf("unlink function start.\n");

	strcpy(pathtmp,path);
	tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such file.\n");
		return -ENOENT;
	}

	if ((inode[tinode].desc.st.st_mode & S_IFMT) != S_IFREG)
	{
		printf("ERROR : Target is not regular file.\n");
		return -EISDIR;
	}
	for (i = 0; i<MAX_FILE_NUM; i++)
	{
		tdata = get_data_index(tinode,i);
		if (tdata == NO_NEXT_BLOCK)
			break;
		else
			set_empty_dblock(tdata);
	}


	delete_dirorfile(tinode);

	printf("rmdir function complete.\n");
	return 0;
}

int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *info)
{
	char pathtmp[2000];
	addr tinode,tmp;

	
	printf("fuse_readdir function start.\n");

	if (strlen(path) >= 2000)
	{
		printf("ERROR : Path name is too long.\n");
		return -ENAMETOOLONG;
	}

		strcpy(pathtmp, path);
		if(strcmp(pathtmp, "/") == 0)
			tinode = 0;
		else 
			tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such directory.\n");
		return -ENOENT;
	}
	if ((inode[tinode].desc.st.st_mode & S_IFMT) != S_IFDIR)
	{
		printf("ERROR : Target is not directory.\n");
		return -ENOTDIR;
	}

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	tmp = inode[tinode].desc.fchild;
	while(tmp != NO_NEXT_BLOCK)
	{
		filler(buf,inode[tmp].FileName,NULL,0);
		tmp = inode[tmp].desc.next;
	}

	inode[tinode].desc.st.st_atime = time(NULL);

	printf("fuse_readdir function complete");

	return 0;

}

int fuse_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	char pathtmp[2000];
	addr tinode, tdata;
	int i;

	printf("fuse_read function start.\n");

	if (strlen(path) >= 2000)
	{
		printf("ERROR : Path name is too long.\n");
		return -ENAMETOOLONG;
	}

	strcpy(pathtmp,path);
	tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such file.\n");
		return -ENOENT;
	}
	if ((inode[tinode].desc.st.st_mode & S_IFMT) != S_IFREG)
	{
		printf("ERROR : Target is not regular file.\n");
		return -EISDIR;
	}

	for (i = 0; i<MAX_FILE_NUM; i++)
	{
		tdata = get_data_index(tinode,i);
		if (tdata == NO_NEXT_BLOCK)
			break;
		else
			memcpy(buf,&dblock[tdata],sb.iSize);
	}

	inode[tinode].desc.st.st_atime = time(NULL);

	printf("fuse_read function complete.\n");

	return size;

}

int fuse_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	char pathtmp[2000];
	char* ptmp;
	addr tinode, newdblock;
	int datasize, i;

	printf("fuse_write function start.\n");

	if (strlen(path) >= 2000)
	{
		printf("ERROR : Path name is too long.\n");
		return -ENAMETOOLONG;
	}

	strcpy(pathtmp,path);
	tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such file.\n");
		return -ENOENT;
	}

	if ((inode[tinode].desc.st.st_mode & S_IFMT) != S_IFREG)
	{
		printf("ERROR : Target is not regular file.\n");
		return -EISDIR;
	}

	datasize = (strlen(buf) / 256) + 1;
	if(strlen(buf) % 256 == 0)
		datasize--;

	ptmp = buf;
	for (i = 0; i<datasize; i++)
	{
		newdblock = get_empty_dblock();
		if(newdblock == NO_NEXT_BLOCK)
		{
			printf("ERROR : No free block.\n");
			return -ENOMEM;
		}
		else
		{
			strncpy((char*)(&dblock[newdblock]),ptmp,256);
			if(put_data_to_index(tinode, newdblock, i) == -1)
			{
				printf("ERROR : No free block.\n");
				return -ENOMEM;
			}
		}
		ptmp = ptmp + 256;
	}
	put_data_to_index(tinode, NO_NEXT_BLOCK, datasize);
	//data의 끝 표시
	inode[tinode].desc.st.st_size = strlen(buf);
	inode[tinode].desc.st.st_mtime = time(NULL);
	printf("fuse_write function complete.\n");

	return size;
}

int fuse_getattr(const char *path, struct stat *statbuf)
{
	char pathtmp[2000];
	addr tinode;

	printf("fuse_getattr function start.\n");

	if (strlen(path) >= 2000)
	{
		printf("ERROR : Path name is too long.\n");
		return -ENAMETOOLONG;
	}

	strcpy(pathtmp,path);

	if(strcmp(pathtmp,"/") == 0)
		tinode = 0;
	else 
		tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such file.\n");
		return -ENOENT;
	}
	
	memset(statbuf,0,sizeof(struct stat));

	statbuf->st_size = inode[tinode].desc.st.st_size;
	statbuf->st_mode = inode[tinode].desc.st.st_mode;
	statbuf->st_nlink = inode[tinode].desc.st.st_nlink;
	statbuf->st_uid = inode[tinode].desc.st.st_uid;
	statbuf->st_gid = inode[tinode].desc.st.st_gid;
	statbuf->st_atime = inode[tinode].desc.st.st_atime;
	statbuf->st_mtime = inode[tinode].desc.st.st_mtime;

	printf("fuse_getattr complete.\n");
	
	return 0;
}

int fuse_chmod(const char* path, mode_t newmode, struct fuse_file_info *info)
{
	char pathtmp[2000];
	addr tinode;

	printf("fuse_chmod function start.\n");

	if (strlen(path) >= 2000)
	{
		printf("ERROR : Path name is too long.\n");
		return -ENAMETOOLONG;
	}

	strcpy(pathtmp,path);
	tinode = get_path(pathtmp);

	if(tinode == NO_NEXT_BLOCK)
	{
		printf("ERROR : No such file.\n");
		return -ENOENT;
	}

	inode[tinode].desc.st.st_mode = newmode;

	printf("fuse_chmod function complete.\n");

	return 0;
}

int fuse_open(const char *path, struct fuse_file_info *fi)
{
	char pathtmp[2000];
	addr open_fi;
	
	strcpy(pathtmp,path);
	open_fi = get_path(pathtmp);
	
	if (open_fi != NO_NEXT_BLOCK)
		return -ENOENT;
	
	if ((inode[open_fi].desc.st.st_mode & S_IFMT) != S_IFMT) //파일 형식이 아님
		return -ENOENT;
		
	if (((fi->flags & 3) != O_RDONLY) | ((fi->flags & 3) != O_WRONLY) | ((fi->flags & 3) != O_RDWR))	//열 수 없음
		return -EACCES;

	inode[open_fi].desc.st.st_atime = time(NULL);

	return 0;
}

int fuse_opendir(const char *path, struct fuse_file_info *fi)
{
	char pathtmp[2000];
	addr open_fi;
	
	strcpy(pathtmp,path);
	open_fi = get_path(pathtmp);
	
	if (open_fi != NO_NEXT_BLOCK)
		return -ENOENT;
	
	if ((inode[open_fi].desc.st.st_mode & S_IFMT) != S_IFDIR) //디렉토리 형식이 아님
		return -ENOENT;
		
	if (((fi->flags & 3) != O_RDONLY) | ((fi->flags & 3) != O_WRONLY) | ((fi->flags & 3) != O_RDWR))	//열 수 없음
		return -EACCES;

	return 0;
}

static int fuse_utime(const char* path, struct utimbuf* tim){
	addr ino;
	char pathtmp[2000];

	strcpy(pathtmp,path);
	ino = get_path(pathtmp);
	
	if(ino == NO_NEXT_BLOCK)
		return -ENOENT;

	return 0;
}

//int(* fuse_operations::truncate)(const char *, off_t) 
static int fuse_truncate(const char* path, off_t s){
	addr ino;
	char pathtmp[2000];

	strcpy(pathtmp,path);
	ino = get_path(pathtmp);
	
	if(ino == NO_NEXT_BLOCK)
		return -ENOENT;

	return 0;
}

struct fuse_operations fuse_oper = {
	.getattr 		= fuse_getattr,
	.readdir        = fuse_readdir,
	.read           = fuse_read,
	.mkdir          = fuse_mkdir,
	.rmdir          = fuse_rmdir,
	.mknod			= fuse_mknod,
	.unlink			= fuse_unlink,
	//.open 		= fuse_open,
	//.opendir 		= fuse_opendir,
	.write 			= fuse_write,
	.chmod 			= fuse_chmod,
	.utime			= fuse_utime,
	.truncate		= fuse_truncate,
};

int main(int argc, char *argv[])
{
	int i;
	int fusestat;
	FILE* fp = fopen("storage.txt", "rb");
	printf("Loading storage.txt..\n");

	if (fp == NULL) 
	{
		printf("Cannot find storage.txt.. Initialize new file system...\n");
		init_storage();
	}
	else
		load_storage(fp);

	printf("Loading/Initializing file system complete!\n");

	printf("File system by 2014312406 심하영, 2014313366 홍기원 \n");
	printf("inode size = %d, data block size = %d \n", sb.iSize, sb.dSize);
	printf("Total number of block(for inode and data block each) = %d\n", NUM_BLOCK);
	printf("Number of used inode block = %d\n",sb.numUsediBlock);
	printf("Number of used data block = %d\n",sb.numUseddBlock);
	printf("Last access time of file system = %s\n",sb.aTime);

	printf("File system mount..\n");


	fusestat = fuse_main(argc, argv, &fuse_oper, NULL);

	printf("File system unmount..\n");
	printf("Saving new file system...\n");

	fp = fopen("storage.txt", "wb");

	fwrite(&sb,sizeof(sb),1,fp);

	for(i=0; i<NUM_BLOCK; i++) 
		fwrite(&inode[i], sizeof(struct iBlock), 1, fp);

	for(i=0; i<NUM_BLOCK; i++)
		fwrite(&dblock[i], sizeof(struct dBlock), 1, fp);

	fclose(fp);
	
	return 0;
}