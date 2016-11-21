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

#define FILE_MODE S_IRUSR | S_IWUSR | S_IXUSR

int fileDescriptor = -1;
struct SuperBlock superBlock = {0, 0, 0, {0}, 0, {0}, 0, 0, 0, {0}};
int inodeIdOfurrentDirectory = 1;
char pathOfCurrentDirectory[256] = {0};

char* functionList[] = {
	"load",
	"close",
	"initfs",
	"cpin",
	"cpout",
	"mkdir",
	"rm",
	"q",
	"show",
	"cd",
	"ls"
};

#define FUNC_LIST_SIZE 11

//utils
unsigned long u25ToLong(struct u25 val);
void longToU25(struct u25* res, unsigned long val);
int max(int a, int b);
char* scanCommand();
char** createArgs(char* str);
int parseInt(char* str);
int checkNameAvailable(char* str);

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
int findV6ByNameInCertainDirectory(int inodeId, char* str);
int changeV6Directory(char* str);
int readV6(int inodeId, void* buff, size_t size, int offset);
int writeV6(int inodeId, void* buff, size_t size, int offset);
int createV6Directory(int inodeId, char* str);
int createV6File(int inodeId, char* str);
int removeV6(int inodeId);
int getV6Path(int inodeId, char* path);
int getV6Name(int inodeId, char* name);
int getV6Size(int inodeId);
int getV6Type(int inodeId);
int removeV6Dir(int inodeId);

//user level
int loadFile(char* args[]);
int closeFile(char* args[]);
int initialFileSystem(char* args[]);
int copyIn(char* args[]);
int copyOut(char* args[]);
int makeDirectory(char* args[]);
int removeFile(char* args[]);
int quit(char* args[]);
int changeDirectory(char* args[]);
int show(char* args[]);
int list(char* args[]);

int (*delegation[])(char* args[]) = {
	&loadFile,
	&closeFile,
	&initialFileSystem,
	&copyIn,
	&copyOut,
	&makeDirectory,
	&removeFile,
	&quit,
	&show,
	&changeDirectory,
	&list
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

int checkNameAvailable(char* str) {
	int pointer = 0;

	while(str[pointer] != 0) {
		if(str[pointer] == 34 ||
			str[pointer] == 42 ||
			str[pointer] == 47 ||
			str[pointer] == 60 ||
			str[pointer] == 58 ||
			str[pointer] == 62 ||
			str[pointer] == 63 ||
			str[pointer] == 124) {
			return -1;
		}

		pointer++;
	}

	if(pointer > 14) {
		return -1;
	}

	return 0;
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

	inode.i_flags = 0140777;

	lseek(fileDescriptor, offset, SEEK_SET);
	write(fileDescriptor, &inode, INODE_SIZE);

	unsigned short inodeId = 1;

	//write the first and second element's inodeId
	writeV6(1, &inodeId, 2, 0);
	writeV6(1, &inodeId, 2, 16);
	//write the first and second element's name
	char str[14] = {0};
	sprintf(str, ".");
	writeV6(1, str, 14, 2);
	sprintf(str, "..");
	writeV6(1, str, 14, 18);

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

int findV6FileAndDirectoryByName(char* str) {
	if(str[0] == 0) {
		return -1;
	}

	int inodeId;
	int pointer;

	if(str[0] == '/') {
		inodeId = 1;
		pointer = 1;
	}
	else {
		inodeId = inodeIdOfurrentDirectory;
		pointer = 0;
	}

	char nameStr[15] = {0};
	int p = 0;

	while(str[pointer] != 0 && p <= 14) {
		if(str[pointer] == '/') {
			nameStr[p] = 0;
			inodeId = findV6ByNameInCertainDirectory(inodeId, nameStr);
			if(inodeId == -1) {
				return -1;
			}
			p = 0;
		}
		else {
			nameStr[p] = str[pointer];
			p++;
		}

		pointer++;
	}

	if(p > 14) {
		return -1;
	}

	if(p != 0) {
		nameStr[p] = 0;
		inodeId = findV6ByNameInCertainDirectory(inodeId, nameStr);
	}

	return inodeId;
}

int findV6ByNameInCertainDirectory(int inodeId, char* str) {
	if(str[0] == 0 || inodeId < 1) {
		return -1;
	}

	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	//if the inode is not directory, return -1
	if(!(inode.i_flags & 040000) || (inode.i_flags & 020000)) {
		return -1;
	}

	unsigned long i_size;
	struct u25 u25_size;

	u25_size.low24 = inode.i_size1;
	u25_size.high24 = inode.i_size0;
	u25_size.extra25 = (inode.i_flags & I_SIZE25BIT) ? 1 : 0;

	i_size = u25ToLong(u25_size);

	char nameStr[15] = {0};
	int pointer;
	char findFlag = 0;

	for(pointer = 2; pointer < i_size; pointer += 16) {
		readV6(inodeId, nameStr, 14, pointer);
		if(strcmp(str,nameStr) == 0) {
			findFlag = 1;
			break;
		}
	}

	//if cannot find the file, return -1
	if(!findFlag) {
		return -1;
	}

	unsigned short resInodeId;
	pointer -= 2;

	readV6(inodeId, &resInodeId, 2, pointer);

	return resInodeId;
}

int changeV6Directory(char* str) {
	int inodeId;

	inodeId = findV6FileAndDirectoryByName(str);

	if(inodeId == -1) {
		return -1;
	}

	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	//if the inode is not directory, return -1
	if(!(inode.i_flags & 040000) || (inode.i_flags & 020000)) {
		return -1;
	}

	inodeIdOfurrentDirectory = inodeId;

	//update the current path string
	getV6Path(inodeIdOfurrentDirectory, pathOfCurrentDirectory);

	return 0;
}

int createV6Directory(int inodeId, char* str) {
	if(str[0] == 0) {
		return -1;
	}

	if(checkNameAvailable(str) == -1) {
		return -1;
	}

	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	//if the inode is not directory, return -1
	if(!(inode.i_flags & 040000) || (inode.i_flags & 020000)) {
		return -1;
	}

	unsigned long i_size;
	struct u25 u25_size;

	u25_size.low24 = inode.i_size1;
	u25_size.high24 = inode.i_size0;
	u25_size.extra25 = (inode.i_flags & I_SIZE25BIT) ? 1 : 0;

	i_size = u25ToLong(u25_size);

	char nameStr[15] = {0};
	int pointer;
	char findFlag = 0;

	for(pointer = 2; pointer < i_size; pointer += 16) {
		readV6(inodeId, nameStr, 14, pointer);
		if(strcmp(str,nameStr) == 0) {
			findFlag = 1;
			break;
		}
	}

	//if find the file or directory with the same name, return -1
	if(findFlag) {
		return -1;
	}

	unsigned short newInodeId;

	newInodeId = consumeINode();

	if(newInodeId == -1) {
		return -1;
	}

	int offset = 2 * BLOCK_SIZE + (newInodeId - 1) * INODE_SIZE;
	struct INode newInode = {0,0,0,0,0,0,{0},{0},{0}};

	newInode.i_flags = 0140777;

	lseek(fileDescriptor, offset, SEEK_SET);
	write(fileDescriptor, &newInode, INODE_SIZE);

	//write the first and second element's inodeId
	writeV6(newInodeId, &newInodeId, 2, 0);
	writeV6(newInodeId, &inodeId, 2, 16);
	//write the first and second element's name
	char dirStr[14] = {0};
	sprintf(dirStr, ".");
	writeV6(newInodeId, dirStr, 14, 2);
	sprintf(dirStr, "..");
	writeV6(newInodeId, dirStr, 14, 18);

	unsigned short temp;

	for(pointer = 0; pointer < i_size; pointer += 16) {
		readV6(inodeId, &temp, 2, pointer);
		if(temp == 0) {
			break;
		}
	}

	writeV6(inodeId, &newInodeId, 2, pointer);
	sprintf(dirStr, "%s", str);
	writeV6(inodeId, dirStr, 14, pointer + 2);

	return newInodeId;
}

int createV6File(int inodeId, char* str) {
	if(str[0] == 0) {
		return -1;
	}

	if(checkNameAvailable(str) == -1) {
		return -1;
	}

	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	//if the inode is not directory, return -1
	if(!(inode.i_flags & 040000) || (inode.i_flags & 020000)) {
		return -1;
	}

	unsigned long i_size;
	struct u25 u25_size;

	u25_size.low24 = inode.i_size1;
	u25_size.high24 = inode.i_size0;
	u25_size.extra25 = (inode.i_flags & I_SIZE25BIT) ? 1 : 0;

	i_size = u25ToLong(u25_size);

	char nameStr[15] = {0};
	int pointer;
	char findFlag = 0;

	for(pointer = 2; pointer < i_size; pointer += 16) {
		readV6(inodeId, nameStr, 14, pointer);
		if(strcmp(str,nameStr) == 0) {
			findFlag = 1;
			break;
		}
	}

	//if find the file or directory with the same name, return -1
	if(findFlag) {
		return -1;
	}

	unsigned short newInodeId;

	newInodeId = consumeINode();

	if(newInodeId == -1) {
		return -1;
	}

	int offset = 2 * BLOCK_SIZE + (newInodeId - 1) * INODE_SIZE;
	struct INode newInode = {0,0,0,0,0,0,{0},{0},{0}};

	newInode.i_flags = 0100777;

	lseek(fileDescriptor, offset, SEEK_SET);
	write(fileDescriptor, &newInode, INODE_SIZE);

	unsigned short temp;

	for(pointer = 0; pointer < i_size; pointer += 16) {
		readV6(inodeId, &temp, 2, pointer);
		if(temp == 0) {
			break;
		}
	}

	writeV6(inodeId, &newInodeId, 2, pointer);
	sprintf(nameStr, "%s", str);
	writeV6(inodeId, nameStr, 14, pointer + 2);

	return newInodeId;
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
	char emptyFlag;
	int dataOffset;
	int countSize;
	int stepSize;
	int b, b1;
	int pointer;
	char* buffer = (char*)buff;
	int indirectBlockId_1, indirectBlockId_2;

	//read data from large file
	if(inode.i_flags & I_LARGE_FILE) {
		emptyFlag = 0;
		countSize = 0;
		pointer = 0;

		while(countSize < size) {
			b = (offset + countSize) / 512;
			b1 = b / 256;

			if(b1 < 7) {
				indirectBlockId_1 = inode.i_addr[b1];
				if(indirectBlockId_1 == 0) {
					emptyFlag = 1;
				}
				else {
					blockId = getIndirectBlockAddress(indirectBlockId_1, b % 256);
					if(blockId == 0) {
						emptyFlag = 1;
					}
				}
			}
			else {
				b1 = b1 - 7;

				indirectBlockId_2 = inode.i_addr[7];
				if(indirectBlockId_2 == 0) {
					emptyFlag = 1;
				}
				else {
					indirectBlockId_1 = getIndirectBlockAddress(indirectBlockId_2, b1);
					if(indirectBlockId_1 == 0) {
						emptyFlag = 1;
					}
					else {
						blockId = getIndirectBlockAddress(indirectBlockId_1, b % 256);
						if(blockId == 0) {
							emptyFlag = 1;
						}
					}
				}
			}

			dataOffset = blockId * BLOCK_SIZE + ((offset + countSize) % 512);
			stepSize = 512 - ((offset + countSize) % 512);
			countSize += stepSize;
			if(countSize > size) {
				stepSize -= countSize - size;
			}

			if(emptyFlag) {
				int i;
				int endPosition = stepSize + pointer;
				for(i = pointer; i < endPosition; i++) {
					buffer[i] = 0;
				}
			}
			else {
				lseek(fileDescriptor, dataOffset, SEEK_SET);
				read(fileDescriptor, &buffer[pointer], stepSize);
			}

			pointer += stepSize;
		}
	}
	//read data from small file
	else {
		emptyFlag = 0;
		countSize = 0;
		pointer = 0;

		while(countSize < size) {
			b = (offset + countSize) / 512;

			blockId = inode.i_addr[b];
			if(blockId == 0) {
				emptyFlag = 1;
			}

			dataOffset = blockId * BLOCK_SIZE + ((offset + countSize) % 512);
			stepSize = 512 - ((offset + countSize) % 512);
			countSize += stepSize;
			if(countSize > size) {
				stepSize -= countSize - size;
			}

			if(emptyFlag) {
				int i;
				int endPosition = stepSize + pointer;
				for(i = pointer; i < endPosition; i++) {
					buffer[i] = 0;
				}
			}
			else {
				lseek(fileDescriptor, dataOffset, SEEK_SET);
				read(fileDescriptor, &buffer[pointer], stepSize);
			}

			pointer += stepSize;
		}
	}

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

int removeV6(int inodeId) {
	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	unsigned long i_size = getV6Size(inodeId);

	int i, k;
	int countSize = 0;
	unsigned short blockId, indirectBlockId;

	if((inode.i_flags & I_LARGE_FILE) == I_LARGE_FILE) {
		for(i = 0; i < 263; i++) {
			if(i < 7) {
				indirectBlockId = inode.i_addr[i];
			}
			else if(inode.i_addr[7] != 0) {
				indirectBlockId = getIndirectBlockAddress(inode.i_addr[7], i - 7);
			}
			else {
				releaseINode(inodeId);
				return 0;
			}

			if(indirectBlockId != 0) {
				for(k = 0; k < 256; k++) {
					blockId = getIndirectBlockAddress(indirectBlockId, k);
					if(blockId != 0) {
						releaseBlock(blockId);
					}

					countSize += 512;

					if(countSize >= i_size) {
						if(inode.i_addr[7] != 0) {
							releaseBlock(inode.i_addr[7]);
						}
						releaseBlock(indirectBlockId);
						releaseINode(inodeId);
						return 0;
					}
				}

				releaseBlock(indirectBlockId);
			}
			else {
				countSize += 131072;

				if(countSize >= i_size) {
					if(inode.i_addr[7] != 0) {
						releaseBlock(inode.i_addr[7]);
					}
					releaseINode(inodeId);
					return 0;
				}
			}
		}
	}
	else {
		for(i = 0; i < 8; i++) {
			blockId = inode.i_addr[i];
			if(blockId != 0) {
				releaseBlock(blockId);
			}
		}
	}

	releaseINode(inodeId);
	return 0;
}

int getV6Path(int inodeId, char* path) {
	if(inodeId == 1) {
		sprintf(path, "/");
		return 0;
	}

	unsigned short parentId;

	readV6(inodeId, &parentId, 2, 16);

	getV6Path(parentId, path);

	char name[15] = {0};

	getV6Name(inodeId, name);

	if(parentId != 1) {
		strcat(path, "/");
	}

	strcat(path, name);

	return 0;
}

int getV6Name(int inodeId, char* name) {
	if(inodeId == 1) {
		sprintf(name, "/");
		return 0;
	}

	unsigned short parentId;

	readV6(inodeId, &parentId, 2, 16);

	int size = getV6Size(parentId);
	unsigned short temp;
	unsigned short pointer;

	for(pointer = 0; pointer < size; pointer += 16) {
		readV6(parentId, &temp, 2, pointer);

		if(temp == inodeId) {
			break;
		}
	}

	readV6(parentId, name, 14, pointer + 2);

	return 0;
}

int getV6Size(int inodeId) {
	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	struct u25 u25_size;

	u25_size.low24 = inode.i_size1;
	u25_size.high24 = inode.i_size0;
	u25_size.extra25 = (inode.i_flags & I_SIZE25BIT) ? 1 : 0;

	return u25ToLong(u25_size);
}

int getV6Type(int inodeId) {
	struct INode inode;
	int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

	//load inode from the file
	lseek(fileDescriptor, inodeOffset, SEEK_SET);
	read(fileDescriptor, &inode, INODE_SIZE);

	return inode.i_flags & I_FILE_TYPE;
}

int removeV6Dir(int inodeId) {
	int i_size = getV6Size(inodeId);
	int pointer;
	unsigned short temp;

	for(pointer = 32; pointer < i_size; pointer += 16) {
		readV6(inodeId, &temp, 2, pointer);
		if(temp != 0) {
			if(getV6Type(temp) == I_DIRECTORY) {
				removeV6Dir(temp);
			}
			else {
				removeV6(temp);
			}
		}
	}

	removeV6(inodeId);
}

//open file
//if the file does not exist, create a new file and open it
int loadFile(char* args[]) {
	if(fileDescriptor != -1) {
		close(fileDescriptor);
		fileDescriptor = -1;
	}

	fileDescriptor = open(args[1], O_CREAT | O_RDWR, FILE_MODE);

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
	if(args[1] == NULL) {
		printf("cannot find parameter\n");
		return -1;
	}

	int externalFileDescriptor = -1;

	externalFileDescriptor = open(args[1], O_RDONLY, FILE_MODE);

	if(externalFileDescriptor == -1) {
		printf("fail to find external file\n");
		return -1;
	}

	int inodeId;

	inodeId = createV6File(inodeIdOfurrentDirectory, args[1]);

	if(inodeId == -1) {
		printf("fail to create V6 file\n");
		return -1;
	}

	int size;

	size = lseek(externalFileDescriptor, 0, SEEK_END);

	printf("file size: %d byte\n", size);

	int countSize = 0;
	int stepSize = 512;
	char buffer[512] = {0};

	while(size > countSize) {
		if(stepSize + countSize > size) {
			stepSize = size - countSize;
		}

		lseek(externalFileDescriptor, countSize, SEEK_SET);
		read(externalFileDescriptor, buffer, stepSize);
		writeV6(inodeId, buffer, stepSize, countSize);

		countSize += stepSize;
	}

	close(externalFileDescriptor);

	printf("copy in succeed\n");

	return 0;
}

int copyOut(char* args[]) {
	if(args[1] == NULL) {
		printf("cannot find parameter\n");
		return -1;
	}

	int inodeId;

	inodeId = findV6ByNameInCertainDirectory(inodeIdOfurrentDirectory, args[1]);

	if(inodeId == -1) {
		printf("fail to find V6 file\n");
		return -1;
	}

	int externalFileDescriptor = -1;

	char nameStr[20] = {0};

	sprintf(nameStr, "V6-%s", args[1]);

	externalFileDescriptor = creat(nameStr, FILE_MODE);

	if(externalFileDescriptor == -1) {
		printf("fail to create external file\n");
		return -1;
	}

	int size;

	size = getV6Size(inodeId);

	printf("file size: %d byte\n", size);

	int countSize = 0;
	int stepSize = 512;
	char buffer[512] = {0};

	while(size > countSize) {
		if(stepSize + countSize > size) {
			stepSize = size - countSize;
		}

		readV6(inodeId, buffer, stepSize, countSize);
		lseek(externalFileDescriptor, countSize, SEEK_SET);
		write(externalFileDescriptor, buffer, stepSize);

		countSize += stepSize;
	}

	close(externalFileDescriptor);

	printf("copy out succeed\n");

	return 0;
}

int makeDirectory(char* args[]) {
	if(args[1] != NULL) {
		if(createV6Directory(inodeIdOfurrentDirectory, args[1]) == -1) {
			printf("fail to create directory\n");
		}
	}
	else {
		printf("cannot find parameter\n");
		return -1;
	}
}

int removeFile(char* args[]) {
	if(args[1] == NULL) {
		printf("fail to find argument\n");
		return -1;
	}

	char* str = args[1];
	int i_size = getV6Size(inodeIdOfurrentDirectory);
	char nameStr[15] = {0};
	int pointer;
	char findFlag = 0;

	for(pointer = 2; pointer < i_size; pointer += 16) {
		readV6(inodeIdOfurrentDirectory, nameStr, 14, pointer);
		if(strcmp(str,nameStr) == 0) {
			findFlag = 1;
			break;
		}
	}

	//if cannot find the file, return -1
	if(!findFlag) {
		printf("fail to find the file or directory\n");
		return -1;
	}

	unsigned short resInodeId;
	pointer -= 2;

	readV6(inodeIdOfurrentDirectory, &resInodeId, 2, pointer);

	char empty[16] = {0};
	writeV6(inodeIdOfurrentDirectory, empty, 16, pointer);

	if(getV6Type(resInodeId) == I_DIRECTORY) {
		removeV6Dir(resInodeId);
	}
	else {
		removeV6(resInodeId);
	}

	return 0;
}

int quit(char* args[]) {
	closeFile(args);
	exit(0);
}

int changeDirectory(char* args[]) {
	if(args[1] != NULL) {
		if(changeV6Directory(args[1]) == -1) {
			printf("wrong directory\n");
		}
	}
	else {
		printf("cannot find parameter\n");
		return -1;
	}
}

int list(char* args[]) {
	int size;

	size = getV6Size(inodeIdOfurrentDirectory);

	char nameStr[15] = {0};
	unsigned short inodeId;
	int pointer;

	printf("inode#\tname\n");
	printf("------------------------\n");

	for(pointer = 0; pointer < size; pointer += 16) {
		readV6(inodeIdOfurrentDirectory, &inodeId, 2, pointer);

		if(inodeId == 0) {
			continue;
		}

		readV6(inodeIdOfurrentDirectory, nameStr, 14, pointer + 2);
		printf("%d\t%s\n", inodeId, nameStr);
	}

	return 0;
}

int show(char* args[]) {
	int i;

	if(args[1] != NULL && strcmp(args[1], "super") == 0) {
		printf("s_isize: %d\n", superBlock.s_isize);
		printf("s_fsize: %d\n", superBlock.s_fsize);
		printf("s_nfree: %d\n", superBlock.s_nfree);
		printf("s_free: ");
		for(i = 0; i < superBlock.s_nfree; i++) {
			printf("%d ", superBlock.s_free[i]);
		}
		printf("\ns_ninode: %d\n", superBlock.s_ninode);
		printf("s_inode: ");
		for(i = 0; i < superBlock.s_ninode; i++) {
			printf("%d ", superBlock.s_inode[i]);
		}
		printf("\ns_flock: %d\n", superBlock.s_flock);
		printf("s_ilock: %d\n", superBlock.s_ilock);
		printf("s_fmod: %d\n", superBlock.s_fmod);
		printf("s_time: %d\n", superBlock.s_time[0] + superBlock.s_time[1] * 0200000);
	}
	else if(args[1] != NULL && strcmp(args[1], "inode") == 0) {
		if(args[2] != NULL) {
			int inodeId = parseInt(args[2]);
			if(inodeId != -1 && inodeId <= superBlock.s_isize * 16) {
				struct INode inode;
				int inodeOffset = 2 * BLOCK_SIZE + (inodeId - 1) * INODE_SIZE;

				//load inode from the file
				lseek(fileDescriptor, inodeOffset, SEEK_SET);
				read(fileDescriptor, &inode, INODE_SIZE);

				printf("i_flags:\n");
				printf("\tallcated: %d\n", (inode.i_flags & I_ALLOCATED) ? 1 : 0);
				printf("\tfile type: ");
				if(inode.i_flags & 040000) {
					if(inode.i_flags & 020000) {
						printf("block special file\n");
					}
					else {
						printf("directory\n");
					}
				}
				else {
					if(inode.i_flags & 020000) {
						printf("char special file\n");
					}
					else {
						printf("plain file\n");
					}
				}
				printf("\tlarge file: %d\n", (inode.i_flags & I_LARGE_FILE) ? 1 : 0);
				printf("\tset uid on execution: %d\n", (inode.i_flags & I_SET_UID) ? 1 : 0);
				printf("\tset group id on execution: %d\n", (inode.i_flags & I_SET_GID) ? 1 : 0);
				printf("\trwx for owner: ");
				if(inode.i_flags & 0400) {
					printf("r ");
				}
				if(inode.i_flags & 0200) {
					printf("w ");
				}
				if(inode.i_flags & 0100) {
					printf("x ");
				}
				printf("\n\trwx for group: ");
				if(inode.i_flags & 040) {
					printf("r ");
				}
				if(inode.i_flags & 020) {
					printf("w ");
				}
				if(inode.i_flags & 010) {
					printf("x ");
				}
				printf("\n\trwx for other: ");
				if(inode.i_flags & 04) {
					printf("r ");
				}
				if(inode.i_flags & 02) {
					printf("w ");
				}
				if(inode.i_flags & 01) {
					printf("x ");
				}
				printf("\n");

				printf("i_nlinks: %d\n", inode.i_nlinks);
				printf("i_uid: %d\n", inode.i_uid);
				printf("i_gid: %d\n", inode.i_gid);
				printf("i_size: %d\n", getV6Size(inodeId));
				printf("i_addr: ");
				for(i = 0; i < 8; i++) {
					printf("%d ", inode.i_addr[i]);
				}
				printf("\ns_time: %d\n", inode.i_atime[0] + inode.i_atime[1] * 0200000);
				printf("s_time: %d\n", inode.i_mtime[0] + inode.i_mtime[1] * 0200000);
			}
			else {
				printf("please give the correct inode id\n");
			}
		}
		else {
			printf("please give the inode id\n");
		}
	}
	else {
		printf("no such argument: %s\n", args[1]);
	}

	return 0;
}

int main(int args, char* argv[]) {
	char* command;
	char** arg;

	if(args >= 2 && loadFile(argv) == -1) {
		printf("fail to open or create file\n");
		return -1;
	}

	pathOfCurrentDirectory[0] = '/';

	printf("/////////////////////////////\n");
    printf("////////// FSAccess /////////\n");
    printf("/////////////////////////////\n");

    while(1) {
    	printf("{FSAccess:%s} ", pathOfCurrentDirectory);
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