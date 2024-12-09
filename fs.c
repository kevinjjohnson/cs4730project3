#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
		int i, index, inode_index = 0;


		// load superblock, inodeMap, blockMap and inodes into the memory
		if(disk_mount(name) == 1) {
				disk_read(0, (char*) &superBlock);
				if(superBlock.magicNumber != MAGIC_NUMBER) {
						printf("Invalid disk!\n");
						exit(0);
				}
				disk_read(1, inodeMap);
				disk_read(2, blockMap);
				for(i = 0; i < numInodeBlock; i++)
				{
						index = i+3;
						disk_read(index, (char*) (inode+inode_index));
						inode_index += (BLOCK_SIZE / sizeof(Inode));
				}
				// root directory
				curDirBlock = inode[0].directBlock[0];
				disk_read(curDirBlock, (char*)&curDir);

		} else {
				// Init file system superblock, inodeMap and blockMap
				superBlock.magicNumber = MAGIC_NUMBER;
				superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
				superBlock.freeInodeCount = MAX_INODE;

				//Init inodeMap
				for(i = 0; i < MAX_INODE / 8; i++)
				{
						set_bit(inodeMap, i, 0);
				}
				//Init blockMap
				for(i = 0; i < MAX_BLOCK / 8; i++)
				{
						if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
						else set_bit(blockMap, i, 0);
				}
				//Init root dir
				int rootInode = get_free_inode();
				curDirBlock = get_free_block();

				inode[rootInode].type = directory;
				inode[rootInode].owner = 0;
				inode[rootInode].group = 0;
				gettimeofday(&(inode[rootInode].created), NULL);
				gettimeofday(&(inode[rootInode].lastAccess), NULL);
				inode[rootInode].size = 1;
				inode[rootInode].blockCount = 1;
				inode[rootInode].directBlock[0] = curDirBlock;

				curDir.numEntry = 1;
				strncpy(curDir.dentry[0].name, ".", 1);
				curDir.dentry[0].name[1] = '\0';
				curDir.dentry[0].inode = rootInode;
				disk_write(curDirBlock, (char*)&curDir);
		}
		return 0;
}

int fs_umount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
		int i, index, inode_index = 0;
		disk_write(0, (char*) &superBlock);
		disk_write(1, inodeMap);
		disk_write(2, blockMap);

		for(i = 0; i < numInodeBlock; i++)
		{
			index = i+3;
			disk_write(index, (char*) (inode+inode_index));
			inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// current directory
		disk_write(curDirBlock, (char*)&curDir);

		disk_umount(name);
}

int search_cur_dir(char *name)
{
		// return inode. If not exist, return -1
		int i;

		for(i = 0; i < curDir.numEntry; i++)
		{
				if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
		}
		return -1;
}

int file_create(char *name, int size)
{
		int i;

		if(size > SMALL_FILE) {
				printf("Do not support files larger than %d bytes.\n", SMALL_FILE);
				return -1;
		}

		if(size < 0){
				printf("File create failed: cannot have negative size\n");
				return -1;
		}

		int inodeNum = search_cur_dir(name);
		if(inodeNum >= 0) {
				printf("File create failed:  %s exist.\n", name);
				return -1;
		}

		if(curDir.numEntry + 1 > MAX_DIR_ENTRY) {
				printf("File create failed: directory is full!\n");
				return -1;
		}

		int numBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numBlock++;

		if(numBlock > superBlock.freeBlockCount) {
				printf("File create failed: data block is full!\n");
				return -1;
		}

		if(superBlock.freeInodeCount < 1) {
				printf("File create failed: inode is full!\n");
				return -1;
		}

		char *tmp = (char*) malloc(sizeof(int) * size + 1);

		rand_string(tmp, size);
		printf("New File: %s\n", tmp);

		// get inode and fill it
		inodeNum = get_free_inode();
		if(inodeNum < 0) {
				printf("File_create error: not enough inode.\n");
				return -1;
		}

		inode[inodeNum].type = file;
		inode[inodeNum].owner = 1;  // pre-defined
		inode[inodeNum].group = 2;  // pre-defined
		gettimeofday(&(inode[inodeNum].created), NULL);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		inode[inodeNum].size = size;
		inode[inodeNum].blockCount = numBlock;
		inode[inodeNum].link_count = 1;

		// add a new file into the current directory entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = inodeNum;
		printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
		curDir.numEntry++;

		// get data blocks
		for(i = 0; i < numBlock; i++)
		{
				int block = get_free_block();
				if(block == -1) {
						printf("File_create error: get_free_block failed\n");
						return -1;
				}
				//set direct block
				inode[inodeNum].directBlock[i] = block;

				disk_write(block, tmp+(i*BLOCK_SIZE));
		}

		//update last access of current directory
		gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);

		printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

		free(tmp);
		return 0;
}

int file_cat(char *name)
{
		int inodeNum, i, size;
		char str_buffer[512];
		char * str;

		//get inode
		inodeNum = search_cur_dir(name);
		size = inode[inodeNum].size;

		//check if valid input
		if(inodeNum < 0)
		{
				printf("cat error: file not found\n");
				return -1;
		}
		if(inode[inodeNum].type == directory)
		{
				printf("cat error: cannot read directory\n");
				return -1;
		}

		//allocate str
		str = (char *) malloc( sizeof(char) * (size+1) );
		str[ size ] = '\0';

		for( i = 0; i < inode[inodeNum].blockCount; i++ ){
				int block;
				block = inode[inodeNum].directBlock[i];

				disk_read( block, str_buffer );

				if( size >= BLOCK_SIZE )
				{
						memcpy( str+i*BLOCK_SIZE, str_buffer, BLOCK_SIZE );
						size -= BLOCK_SIZE;
				}
				else
				{
						memcpy( str+i*BLOCK_SIZE, str_buffer, size );
				}
		}
		printf("%s\n", str);

		//update lastAccess
		gettimeofday( &(inode[inodeNum].lastAccess), NULL );

		free(str);

		//return success
		return 0;
}

int file_read(char *name, int offset, int size)
{
	int inodeNum, readSize, i, readBlock, readOffset, first;
	char buff[512];
	char* output;

	inodeNum = search_cur_dir(name);

	//check if valid input
	if(inodeNum < 0)
	{
		printf("read error: file not found\n");
		return -1;
	}
	if(inode[inodeNum].type == directory)
	{
		printf("read error: cannot read directory\n");
		return -1;
	}
	if(inode[inodeNum].size < (size + offset)){
		printf("read error: read is too large\n");
		return -1;
	}

	output = (char *) malloc(sizeof(char) * (size + 1));
	output[size] = '\0';

	readBlock = offset / BLOCK_SIZE;
	readOffset = offset % BLOCK_SIZE;
	readSize = size;

	int cur = 0;
	for(i = readBlock; i < inode[inodeNum].blockCount; i++){
		int block;
		block = inode[inodeNum].directBlock[i];
		disk_read(block, buff);

		if(readSize >= BLOCK_SIZE){
			memcpy((output + cur), buff + readOffset, BLOCK_SIZE - readOffset);
			cur+= BLOCK_SIZE - readOffset;
			readSize -= (BLOCK_SIZE - readOffset);
		}else{
			memcpy( (output + cur), buff + readOffset, readSize);
			cur+= readSize - 1;
			readSize -= (BLOCK_SIZE - readOffset);
			if(readSize < 0) readSize = 0;
		}

		readOffset = 0;
	}
	printf("%s\n", output);
	gettimeofday( &(inode[inodeNum].lastAccess), NULL );
	free(output);
	return 0;
}

int file_stat(char *name)
{
		char timebuf[28];
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file cat error: file is not exist.\n");
				return -1;
		}

		printf("Inode\t\t= %d\n", inodeNum);
		if(inode[inodeNum].type == file) printf("type\t\t= File\n");
		else printf("type\t\t= Directory\n");
		printf("owner\t\t= %d\n", inode[inodeNum].owner);
		printf("group\t\t= %d\n", inode[inodeNum].group);
		printf("size\t\t= %d\n", inode[inodeNum].size);
		printf("link_count\t= %d\n", inode[inodeNum].link_count);
		printf("num of block\t= %d\n", inode[inodeNum].blockCount);
		format_timeval(&(inode[inodeNum].created), timebuf, 28);
		printf("Created time\t= %s\n", timebuf);
		format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
		printf("Last acc. time\t= %s\n", timebuf);
}

int file_remove(char *name)
{
	int inodeNum, i;

	inodeNum = search_cur_dir(name);

	//check if valid input
	if(inodeNum < 0)
	{
		printf("remove error: file not found\n");
		return -1;
	}
	if(inode[inodeNum].type == directory)
	{
		printf("remove error: cannot remove directory\n");
		return -1;
	}
	if(inode[inodeNum].link_count > 1){
		printf("remove error: cannot remove file with hard links\n");
		return -1;
	}

	//find the file to be removed in the curdir
	int ind = 0;
	for(i = 0; i < curDir.numEntry; i++){
		if(curDir.dentry[i].inode == inodeNum)ind = i;
	}

	//replace file in dir with the last one and then set count - 1
	if(ind != curDir.numEntry - 1){
		curDir.dentry[ind].inode = curDir.dentry[curDir.numEntry - 1].inode;
		strncpy(curDir.dentry[ind].name, curDir.dentry[curDir.numEntry - 1].name, strlen(curDir.dentry[curDir.numEntry - 1].name));
	}
	curDir.numEntry--;

	//set curdir last modified to current time
	gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);

	//free data blocks
	for(i = 0; i < inode[inodeNum].blockCount; i++){
		set_bit(blockMap, inode[inodeNum].directBlock[i], 0);
		superBlock.freeBlockCount++;
	}

	//free inode block
	set_bit(inodeMap, inodeNum, 0);
	superBlock.freeInodeCount++;

	return 0;
}

int dir_make(char* name)
{
	int inodeNum, readSize, i, readBlock, readOffset, first;
	char buff[512];
	char* output;
    Dentry newDir;

	inodeNum = search_cur_dir(name);

	//check if valid input
	if(inodeNum >= 0)
	{
		printf("mkdir error: directory already exists\n");
		return -1;
	}
	if(curDir.numEntry == MAX_DIR_ENTRY){
		printf("mkdir: directory is full\n");
		return -1;
	}
	if(1 > superBlock.freeBlockCount) {
		printf("File create failed: data block is full!\n");
		return -1;
	}
	if(superBlock.freeInodeCount < 1) {
		printf("File create failed: inode is full!\n");
		return -1;
	}

	//Init new dir
	int newInode = get_free_inode();
	int newDirBlock = get_free_block();

	inode[newInode].type = directory;
	inode[newInode].owner = 0;
	inode[newInode].group = 0;
	gettimeofday(&(inode[newInode].created), NULL);
	gettimeofday(&(inode[newInode].lastAccess), NULL);
	inode[newInode].size = 1;
	inode[newInode].blockCount = 1;
	inode[newInode].directBlock[0] = newDirBlock;

	//init new dir
	newDir.numEntry = 2;
	
	//make current dir entry as the new dir
	strncpy(newDir.dentry[0].name, ".", 1);
	newDir.dentry[0].name[1] = '\0';
	newDir.dentry[0].inode = newInode;

	//parent dir entry is current dir 
	strncpy(newDir.dentry[1].name, "..", 2);
	newDir.dentry[1].name[2] = '\0';
	newDir.dentry[1].inode = curDir.dentry[0].inode;
	
	//write dentry to disk
	disk_write(newDirBlock, (char*)&newDir);

	//STILL NEED TO UPDATE CURRENT DIR
	strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
	curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
	curDir.dentry[curDir.numEntry].inode = newInode;
	curDir.numEntry++;

	return 0;
}

int dir_remove(char *name)
{
	int inodeNum, dirBlock, i;
	Dentry dir;

	inodeNum = search_cur_dir(name);

	if(inodeNum < 0)
	{
		printf("rmdir error: directory not found\n");
		return -1;
	}
	if(inode[inodeNum].type == file)
	{
		printf("rmdir error: cannot remove file\n");
		return -1;
	}

	dirBlock = inode[inodeNum].directBlock[0];
	disk_read(dirBlock, (char*)&dir);
	//if dir has more than just . and ..
	if(dir.numEntry != 2){
		//change into inner dir
		dir_change(name);
		//for each entry in dir
		for(int i = 2; i < dir.numEntry; i++){
			//try and remove that entry
			int res = dir_remove(dir.dentry[i].name);
			//if failed, cascade failure up
			if(res == -1)
				return res;
		}
		//change back to parent dir
		dir_change("..");
	}
	//remove the directory
	printf(name);
	printf("\n");

	printf("looking for inode: %d\n", inodeNum);
	//find dir we are removing in current dir
	for(i = 1; i < curDir.numEntry; i++){	
		printf("checking against: %d\n", curDir.dentry[i].inode);
		if(curDir.dentry[i].inode == inodeNum){
			//if its the last entry in dir just decrease size
			if(i == curDir.numEntry - 1){
				curDir.numEntry--;
			//else replace it with the last entry and decrease size
			}else{
				curDir.dentry[i] = curDir.dentry[curDir.numEntry - 1];
				curDir.numEntry--;
			}
			break;
		}
	}

	//free datablock
	set_bit(blockMap, inode[inodeNum].directBlock[0], 0);
	//free inode
	set_bit(inodeMap, inodeNum, 0);
	return 0;
}

int dir_change(char* name)
{
		int inodeNum, i;

		//get inode number
		inodeNum = search_cur_dir(name);
		if (inodeNum < 0)
		{
			printf("cd error: %s does not exist\n", name);
			return -1;
		}
		if (inode[inodeNum].type != directory)
		{
				printf("cd error: % is not a directory\n", name);
				return -1;
		}

		//write parent directory (curDir) to disk
		disk_write(curDirBlock, (char*)&curDir);

		//read new directory from disk into curDir
		curDirBlock = inode[inodeNum].directBlock[0];
		disk_read(curDirBlock, (char*)&curDir);

		//update last access of directory we are changing to
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);

		return 0;
}

int ls()
{
		int i;
		for(i = 0; i < curDir.numEntry; i++)
		{
			int n = curDir.dentry[i].inode;
			if(inode[n].type == file) printf("type: file, ");
			else printf("type: dir, ");
			printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
		}

		return 0;
}

int fs_stat()
{
	printf("File System Status: \n");
	printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
}

int hard_link(char *src, char *dest)
{

	int inodeNumSrc, inodeNumDest, i;

	inodeNumSrc = search_cur_dir(src);
	inodeNumDest = search_cur_dir(dest);
	//check if src valid input
	if(inodeNumSrc < 0)
	{
		printf("link error: src file not found\n");
		return -1;
	}
	if(inode[inodeNumSrc].type == directory)
	{
		printf("link error: cannot link directory\n");
		return -1;
	}
	//check if dst valid input
	if(inodeNumDest >= 0)
	{
		printf("link error: destination file already exists\n");
		return -1;
	}
	if(inode[inodeNumDest].type == directory)
	{
		printf("link error: cannot link directory\n");
		return -1;
	}
	if(curDir.numEntry == MAX_DIR_ENTRY){
		printf("link error: directory is full\n");
		return -1;
	}

	//add new entry to the dentry table that points to same inodenum
	strncpy(curDir.dentry[curDir.numEntry].name, dest, strlen(dest));
	curDir.dentry[curDir.numEntry].name[strlen(dest)] = '\0';
	curDir.dentry[curDir.numEntry].inode = inodeNumSrc;
	curDir.numEntry++;

	//increase link count
	inode[inodeNumSrc].link_count++;

	return 0;
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{

    printf ("\n");
	if(command(comm, "df")) {
		return fs_stat();
    // file command start
    } else if(command(comm, "create")) {
        if(numArg < 2) {
            printf("error: create <filename> <size>\n");
            return -1;
        }
		return file_create(arg1, atoi(arg2)); // (filename, size)

	} else if(command(comm, "stat")) {
		if(numArg < 1) {
			printf("error: stat <filename>\n");
			return -1;
		}
		return file_stat(arg1); //(filename)

	} else if(command(comm, "cat")) {
		if(numArg < 1) {
			printf("error: cat <filename>\n");
			return -1;
		}
		return file_cat(arg1); // file_cat(filename)

	} else if(command(comm, "read")) {
		if(numArg < 3) {
			printf("error: read <filename> <offset> <size>\n");
			return -1;
		}
		return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);

	} else if(command(comm, "rm")) {
		if(numArg < 1) {
			printf("error: rm <filename>\n");
			return -1;
		}
		return file_remove(arg1); //(filename)

	} else if(command(comm, "ln")) {
		return hard_link(arg1, arg2); // hard link. arg1: src file or dir, arg2: destination file or dir

    // directory command start
	} else if(command(comm, "ls"))  {
		return ls();

	} else if(command(comm, "mkdir")) {
		if(numArg < 1) {
			printf("error: mkdir <dirname>\n");
			return -1;
		}
		return dir_make(arg1); // (dirname)

	} else if(command(comm, "rmdir")) {
		if(numArg < 1) {
			printf("error: rmdir <dirname>\n");
			return -1;
		}
		return dir_remove(arg1); // (dirname)

	} else if(command(comm, "cd")) {
		if(numArg < 1) {
			printf("error: cd <dirname>\n");
			return -1;
		}
		return dir_change(arg1); // (dirname)

	} else {
		fprintf(stderr, "%s: command not found.\n", comm);
		return -1;
	}
	return 0;
}
