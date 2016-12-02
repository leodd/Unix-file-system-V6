# Unix-file-system-V6

This is a customize V6 file system, which utilize the unused bit in the inode flag, expanding the max file size to 2^25 bytes.

COMMANDS
--
load nameOfFileSystem
	this command can load the existing file system
	if the file doesn't exist, create a new file system with the name given
	ex. load myV6

	here's an alternative choice for loading the file system when you start the program
	ex. ./a.out myV6

initfs blockNum  inodeNum
	this command can initialize the file system that loaded
	ex. initfs 1024 30

show arg1 arg2
	this command can show the content of super block and the content of inode
	for super block, the arg1 is "super"
	ex. show super
	
	for inode, the arg1 is "inode", and the arg2 is the inode number
	ex. show inode 2

ls
	this command can list all the content in the current directory
	ex. ls

cd path
	this command can change the current directory
	for path start from the current directory
	ex. cd aa/bb

	for path start from the root
	ex. cd /aa/bb

	for going back to the parent directory
	ex. cd ..

mkdir nameOfDir
	this command can create a new directory in the current directory
	ex. cd aa

cpin nameOfExternalFile
	this command can copy external file to the current directory
	ex. cpin fsaccess.c

cpout nameOfV6File
	this command can copy file in the current directory to the external file system
	P.S. the external file named "V6-nameOfV6File"
	ex. cpout fsaccess.c

rm nameOfFile
	this command can remove file or directory in the current directory
	P.S. when removing a directory, the content in the directory will also be removed
	ex. rm aa

close
	this command can close the loaded file system
	ex. close

q
	this command can terminate the program
	ex. q
