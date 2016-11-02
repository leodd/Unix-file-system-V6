#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

struct SuperBlock {
	unsigned short s_isize;
	unsigned short s_fsize;
	unsigned short s_nfree;
	unsigned short s_free[100];
	unsigned short s_ninode;
	unsigned short s_inode[100];
	char s_flock;
	char s_ilock;
	char s_fmod;
	unsigned short s_time[2];
};

struct INode{
	unsigned short i_flags;
	char i_nlinks;
	char i_uid;
	char i_gid;
	char i_size0;
	unsigned short i_size1;
	unsigned short i_addr[8];
	unsigned short i_atime[2];
	unsigned short i_mtime[2];
};

#define I_ALLOCATED 0100000
#define I_FILE_TYPE 060000
#define I_PLAIN_FILE 000000
#define I_DIRECTORY 040000
#define I_CHAR_TYPE 020000
#define I_BLOCK_TYPE 060000
#define I_LARGE_FILE 010000
#define I_SET_UID 04000
#define I_SET_GID 02000
#define I_SIZE25BIT 01000
#define I_READ_OWNER 0400
#define I_WRITE_OWNER 0200
#define I_EXEC_OWNER 0100
#define I_RWX_GROUP 070
#define I_RWX_OTHER 07

struct u25{
	unsigned short low24;
	unsigned char high24;
	unsigned char extra25;
};

#define U25MAX_VALUE 0177777777

#define BLOCK_SIZE 512
#define INODE_SIZE 32

int fileDescriptor = -1;
struct SuperBlock superBlock;
int inodeIdOfurrentDirectory = 1;

char* functionList[] = {
	"load",
	"close",
	"initfs",
	"cpin",
	"cpout",
	"mkdir",
	"rm",
	"q"
};

#define FUNC_LIST_SIZE 8

//utils
unsigned long u25ToLong(struct u25 val);
void longToU25(struct u25* res, unsigned long val);
int max(int a, int b);
char* scanCommand();
char** createArgs(char* str);
int parseInt(char* str);

//system level
int loadSuperBlock();
int saveSuperBlock();
int makeFreeBlockChain(int startPosition, int endPosition);
int makeIList(int endPosition);
int fillINodeCacheArray();
int makeRootDirectory();
int getIndirectBlockAddress(int blockId, int num);
int setIndirectBlockAddress(int blockId, int address, int num);
int consumeBlock();
int releaseBlock(int blockId);
int consumeINode();
int releaseINode(int inodeId);

//file level
int findV6FileAndDirectoryByName(char* str);
int changeV6Directory(char* str);
int readV6(int inodeId, void* buff, size_t size, int offset);
int writeV6(int inodeId, void* buff, size_t size, int offset);
int createV6Directory(int inodeId, char* str);
int createV6File(int inodeId, char* str);
int removeV6(int inodeId);

//user level
int loadFile(char* args[]);
int closeFile(char* args[]);
int initialFileSystem(char* args[]);
int copyIn(char* args[]);
int copyOut(char* args[]);
int makeDirectory(char* args[]);
int removeFile(char* args[]);
int quit(char* args[]);

int (*delegation[])(char* args[]) = {
	&loadFile,
	&closeFile,
	&initialFileSystem,
	&copyIn,
	&copyOut,
	&makeDirectory,
	&removeFile,
	&quit
};

//transfer u25 data to unsigned long data
unsigned long u25ToLong(struct u25 val) {
	unsigned long res = 0;

	res = val.low24;
	res += val.high24 * 0200000;
	res += val.extra25 * 0100000000;

	return res;
}

//transfer unsigned long data to u25 data
void longToU25(struct u25* res, unsigned long val) {
	unsigned short temp;

	res->low24 = val;
	temp = val / 0200000;
	res->high24 = temp;
	res->extra25 = temp / 0400;
}

int max(int a, int b) {
	return a > b ? a : b;
}

//scan the command from the console
//and return it as a string
char* scanCommand() {
	int size = 1;
	int index = -1;
	char* str = (char*)malloc(size * sizeof(char));
	char c;

	while(1) {
		c = getchar();

		index++;

		if(index >= size) {
			size++;
			str = (char*)realloc(str, size * sizeof(char));
		}

		if(c != '\n') {
			str[index] = c;
		}
		else {
			str[index] = 0;
			break;
		}
	}

	return str;
}

//create arguments by spliting the command string
//and return it as an array of arguments
char** createArgs(char* str) {
	int size = 2;
	int index = 1;
	char** args = (char**)malloc(size * sizeof(char*));
	int pointer = 0;

	args[0] = &str[0];

	while(str[pointer] != 0) {
		if(str[pointer] == ' ') {
			size++;
			args = (char**)realloc(args, size * sizeof(char*));
			str[pointer] = 0;
			args[index] = &str[pointer + 1];
			index++;
		}

		pointer++;
	}

	args[index] = NULL;

	return args;
}

//parse string into int
//the function can only parse positive integer
//it will return -1 if the string contains characters other than numbers
int parseInt(char* str) {
	int pointer = 0;
	int val = 0;

	while(str[pointer] != 0) {
		if(str[pointer] >= '0' && str[pointer] <= '9') {
			val = 10 * val + (str[pointer] - '0');
			pointer++;
		}
		else {
			return -1;
		}
	}

	return val;
}

//load super block from the file
int loadSuperBlock() {
	//load super block from the file
	lseek(fileDescriptor, BLOCK_SIZE, SEEK_SET);
	read(fileDescriptor, &superBlock, sizeof(struct SuperBlock));

	return 0;
}

//save super block to the file
int saveSuperBlock() {
	time_t t;
	time(&t);
	superBlock.s_time[0] = t % 0200000;
	superBlock.s_time[1] = t / 0200000;

	//write super block into the file
	lseek(fileDescriptor, BLOCK_SIZE, SEEK_SET);
	write(fileDescriptor, &superBlock, sizeof(struct SuperBlock));

	return 0;
}

//given the start position and the end position
//the function will add the block from end position to start position to the free block chain
int makeFreeBlockChain(int startPosition, int endPosition) {
	//initialize the first element in the free list
	//this first element will be the inidication of whether the free block chain is empty
	superBlock.s_free[0] = 0;
	superBlock.s_nfree = 1;

	//write super block into the file
	saveSuperBlock();

	int pointer;

	for(pointer = startPosition; pointer <= endPosition; pointer++) {
		releaseBlock(pointer);
	}

	return 0;
}

//initialize those blocks which are used as inode
int makeIList(int endPosition) {
	char empty[BLOCK_SIZE] = {0};

	int offset = 2 * BLOCK_SIZE;
	int pointer;

	for(pointer = 2; pointer <= endPosition; pointer++) {
		lseek(fileDescriptor, offset, SEEK_SET);
		write(fileDescriptor, empty, BLOCK_SIZE);
		offset += BLOCK_SIZE;
	}

	return 0;
}

//find inodes which are unallocated
//and add them to the inode cache array in super block
int fillINodeCacheArray() {
	if(superBlock.s_ninode == 100) {
		return 0;
	}

	int offset = 2 * BLOCK_SIZE;
	unsigned short i_flags;
	int iNum;
	int maxNum = superBlock.s_isize * 16;

	for(iNum = 1; iNum <= maxNum; iNum++) {
		lseek(fileDescriptor, offset, SEEK_SET);
		read(fileDescriptor, &i_flags, 2);

		if(!(i_flags & I_ALLOCATED)) {
			superBlock.s_inode[superBlock.s_ninode] = iNum;
			superBlock.s_ninode++;

			if(superBlock.s_ninode == 100) {
				break;
			}
		}

		offset += INODE_SIZE;
	}

	if(superBlock.s_ninode == 0) {
		return -1;
	}

	//write super block into the file
	saveSuperBlock();

	return 0;
}

//make the root directory
int makeRootDirectory() {
	int offset = 2 * BLOCK_SIZE;

	struct INode inode = {0,0,0,0,0,0,{0},{0},{0}};

	inode.i_flags = 0120777;

	lseek(fileDescriptor, offset, SEEK_SET);
	write(fileDescriptor, &inode, INODE_SIZE);

	unsigned short inodeId = 1;

	//write the first and second element's inodeId
	writeV6(1, &inodeId, 2, 0);
	writeV6(1, &inodeId, 2, 16);
	//write the first and second element's name
	writeV6(1, ".", 1, 2);
	writeV6(1, "..", 2, 18);

	return 0;
}

//find # address in an indirect block
int getIndirectBlockAddress(int blockId, int num) {
	unsigned short res;

	lseek(fileDescriptor, (blockId * BLOCK_SIZE) + (num * 2), SEEK_SET);
	read(fileDescriptor, &res, 2);

	return res;
}

//set # address of an indirect block
int setIndirectBlockAddress(int blockId, int address, int num) {
	unsigned short buffer = (unsigned short)address;

	lseek(fileDescriptor, (blockId * BLOCK_SIZE) + (num * 2), SEEK_SET);
	write(fileDescriptor, &buffer, 2);

	return 0;
}

//add the block into free block chain
int releaseBlock(int blockId) {
	if(blockId >= superBlock.s_fsize) {
		return -1;
	}

	if(superBlock.s_nfree == 100) {
		int offset = blockId * BLOCK_SIZE;

		lseek(fileDescriptor, offset, SEEK_SET);
		write(fileDescriptor, &superBlock.s_nfree, 2);
		offset += 2;

		lseek(fileDescriptor, offset, SEEK_SET);
		write(fileDescriptor, superBlock.s_free, 2 * superBlock.s_nfree);

		superBlock.s_free[0] = blockId;
		superBlock.s_nfree = 1;
	}
	else {
		superBlock.s_free[superBlock.s_nfree] = blockId;
		superBlock.s_nfree++;
	}

	//write super block into the file
	saveSuperBlock();

	return 0;
}

//take a block from the free block chain
//and return the block id
//if free block chain is empty, return -1
int consumeBlock() {
	superBlock.s_nfree--;

	if(superBlock.s_free[superBlock.s_nfree] == 0) {
		return -1;
	}

	int blockId;

	if(superBlock.s_nfree == 0) {
		blockId = superBlock.s_free[0];

		int offset = blockId * BLOCK_SIZE;

		lseek(fileDescriptor, offset, SEEK_SET);
		read(fileDescriptor, &superBlock.s_nfree, 2);
		offset += 2;

		lseek(fileDescriptor, offset, SEEK_SET);
		read(fileDescriptor, &superBlock.s_free, 2 * superBlock.s_nfree);
	}
	else {
		blockId = superBlock.s_free[superBlock.s_nfree];
	}

	//write super block into the file
	saveSuperBlock();

	char empty[BLOCK_SIZE] = {0};

	lseek(fileDescriptor, blockId * BLOCK_SIZE, SEEK_SET);
	write(fileDescriptor, empty, BLOCK_SIZE);

	return blockId;
}

int releaseINode(int inodeId) {
	char empty[INODE_SIZE] = {0};
	int offset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	lseek(fileDescriptor, offset, SEEK_SET);
	write(fileDescriptor, empty, INODE_SIZE);

	if(superBlock.s_ninode < 100) {
		superBlock.s_inode[superBlock.s_ninode] = inodeId;
		superBlock.s_ninode++;
	}

	//write super block into the file
	saveSuperBlock();

	return 0;
}

int consumeINode() {
	if(superBlock.s_ninode == 0 && fillINodeCacheArray() == -1) {
		return -1;
	}

	int inodeId;

	superBlock.s_ninode--;
	inodeId = superBlock.s_inode[superBlock.s_ninode];

	//write super block into the file
	saveSuperBlock();

	return inodeId;
}

//read V6 file
int readV6(int inodeId, void* buff, size_t size, int offset) {
	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	unsigned long i_size;
	struct u25 u25_size;

	u25_size.low24 = inode.i_size1;
	u25_size.high24 = inode.i_size0;
	u25_size.extra25 = (inode.i_flags & I_SIZE25BIT) ? 1 : 0;

	i_size = u25ToLong(u25_size);

	int blockId;
	
	return 0;
}

//write to V6 file
int writeV6(int inodeId, void* buff, size_t size, int offset) {
	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	unsigned long i_size;
	struct u25 u25_size;

	u25_size.low24 = inode.i_size1;
	u25_size.high24 = inode.i_size0;
	u25_size.extra25 = (inode.i_flags & I_SIZE25BIT) ? 1 : 0;

	i_size = u25ToLong(u25_size);

	int blockId;

	//if the data to be write exceeds the size of the file, return -1
	if(offset + size > U25MAX_VALUE) {
		return -1;
	}

	//transform small file to large file
	if(!(inode.i_flags & I_LARGE_FILE) && ((offset + size) / 512 > 7)) {
		blockId = consumeBlock();
		if(blockId == -1) {
			return -1;
		}

		lseek(fileDescriptor, blockId * BLOCK_SIZE, SEEK_SET);
		write(fileDescriptor, inode.i_addr, 16);

		int i;
		for(i = 1; i < 8; i++) {
			inode.i_addr[i] = 0;
		}

		inode.i_addr[0] = blockId;

		inode.i_flags |= I_LARGE_FILE;
	}

	int dataOffset;
	int countSize;
	int stepSize;
	int b, b1;
	int pointer;
	char* buffer = (char*)buff;
	int indirectBlockId_1, indirectBlockId_2;

	//write data to large file
	if(inode.i_flags & I_LARGE_FILE) {
		countSize = 0;
		pointer = 0;

		while(countSize < size) {
			b = (offset + countSize) / 512;
			b1 = b / 256;

			if(b1 < 7) {
				indirectBlockId_1 = inode.i_addr[b1];
				if(indirectBlockId_1 == 0) {
					indirectBlockId_1 = consumeBlock();
					if(indirectBlockId_1 == -1) {return -1;}
					inode.i_addr[b1] = indirectBlockId_1;
				}

				blockId = getIndirectBlockAddress(indirectBlockId_1, b % 256);
				if(blockId == 0) {
					blockId = consumeBlock();
					if(blockId == -1) {return -1;}
					setIndirectBlockAddress(indirectBlockId_1, blockId, b % 256);
				}
			}
			else {
				b1 = b1 - 7;

				indirectBlockId_2 = inode.i_addr[7];
				if(indirectBlockId_2 == 0) {
					indirectBlockId_2 = consumeBlock();
					if(indirectBlockId_2 == -1) {return -1;}
					inode.i_addr[7] = indirectBlockId_2;
				}

				indirectBlockId_1 = getIndirectBlockAddress(indirectBlockId_2, b1);
				if(indirectBlockId_1 == 0) {
					indirectBlockId_1 = consumeBlock();
					if(indirectBlockId_1 == -1) {return -1;}
					setIndirectBlockAddress(indirectBlockId_2, indirectBlockId_1, b1);
				}

				blockId = getIndirectBlockAddress(indirectBlockId_1, b % 256);
				if(blockId == 0) {
					blockId = consumeBlock();
					if(blockId == -1) {return -1;}
					setIndirectBlockAddress(indirectBlockId_1, blockId, b % 256);
				}
			}

			dataOffset = blockId * BLOCK_SIZE + ((offset + countSize) % 512);
			stepSize = 512 - ((offset + countSize) % 512);
			countSize += stepSize;
			if(countSize > size) {
				stepSize -= countSize - size;
			}

			lseek(fileDescriptor, dataOffset, SEEK_SET);
			write(fileDescriptor, &buffer[pointer], stepSize);

			pointer += stepSize;
		}
	}
	//write data to small file
	else {
		countSize = 0;
		pointer = 0;

		while(countSize < size) {
			b = (offset + countSize) / 512;

			blockId = inode.i_addr[b];
			if(blockId == 0) {
				blockId = consumeBlock();
				if(blockId == -1) {return -1;}
				inode.i_addr[b] = blockId;
			}

			dataOffset = blockId * BLOCK_SIZE + ((offset + countSize) % 512);
			stepSize = 512 - ((offset + countSize) % 512);
			countSize += stepSize;
			if(countSize > size) {
				stepSize -= countSize - size;
			}

			lseek(fileDescriptor, dataOffset, SEEK_SET);
			write(fileDescriptor, &buffer[pointer], stepSize);

			pointer += stepSize;
		}
	}

	if(i_size < offset + size) {
		i_size = offset + size;
		longToU25(&u25_size, i_size);

		inode.i_size1 = u25_size.low24;
		inode.i_size0 = u25_size.high24;
		inode.i_flags |= u25_size.extra25 ? I_SIZE25BIT : 0;
	}

	//save inode
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	write(fileDescriptor, &inode, INODE_SIZE);

	return 0;
}

//open file
//if the file does not exist, create a new file and open it
int loadFile(char* args[]) {
	if(fileDescriptor != -1) {
		close(fileDescriptor);
		fileDescriptor = -1;
	}

	fileDescriptor = open(args[1], O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

	if(fileDescriptor != -1) {
		//load super block from the file
		loadSuperBlock();
	}

	return fileDescriptor;
}

//close file
int closeFile(char* args[]) {
	close(fileDescriptor);

	fileDescriptor = -1;

	return 0;
}

//initialize the file system
int initialFileSystem(char* args[]) {
	if(fileDescriptor == -1) {
		printf("no loaded file\n");
		return -1;
	}

	if(args[1] == NULL && args[2] == NULL) {
		printf("cannot find parameters\n");
		return -1;
	}

	int numOfBlock = parseInt(args[1]);
	int numOfINode = parseInt(args[2]);

	if(numOfBlock == 0 || numOfINode == 0 || numOfINode > numOfBlock * 16) {
		printf("incorrect parameters\n");
		return -1;
	}

	//extend file
	char emptyBlock[BLOCK_SIZE] = {0};
	lseek(fileDescriptor, (numOfBlock - 1) * BLOCK_SIZE, SEEK_SET);
	write(fileDescriptor, emptyBlock, BLOCK_SIZE);

	//initialize super block
	superBlock.s_isize = numOfINode / 16;
	superBlock.s_isize += (superBlock.s_isize * 16) >= numOfINode ? 0 : 1;
	superBlock.s_fsize = numOfBlock;
	superBlock.s_nfree = 0;
	superBlock.s_ninode = 0;
	superBlock.s_flock = 0;
	superBlock.s_ilock = 0;
	superBlock.s_fmod = 1;

	//write super block to the block[1] of file
	saveSuperBlock();

	makeFreeBlockChain(superBlock.s_isize + 2, superBlock.s_fsize - 1);

	makeIList(superBlock.s_isize + 1);

	makeRootDirectory();

	fillINodeCacheArray();

	return 0;
}

int copyIn(char* args[]) {
	return 0;
}

int copyOut(char* args[]) {
	return 0;
}

int makeDirectory(char* args[]) {
	return 0;
}

int removeFile(char* args[]) {
	return 0;
}

int quit(char* args[]) {
	closeFile(args);
	exit(0);
}

int main(int args, char* argv[]) {
	char* command;
	char** arg;

	if(args >= 2 && loadFile(argv) == -1) {
		printf("fail to open or create file\n");
		return -1;
	}

	printf("/////////////////////////////\n");
    printf("////////// FSAccess /////////\n");
    printf("/////////////////////////////\n");

    while(1) {
    	printf("please enter your command >> ");
    	command = scanCommand();
    	arg = createArgs(command);

    	int i;

    	for(i = 0; i < FUNC_LIST_SIZE; i++) {
    		if(strcmp(functionList[i], arg[0]) == 0) {
    			(*delegation[i])(arg);
    			break;
    		}
    	}
    }

    return 0;
}