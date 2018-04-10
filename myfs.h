#include<bits/stdc++.h>
#include <sys/stat.h> 
#include<fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <chrono>
#include<sys/wait.h>
#include <pthread.h>

using namespace std; 

#define MAX_NO_INODE 20
#define MAX_DATA_BLOCKS 2000
#define BLOCK_SIZE 256
#define PERMISSION "110110100"
#define ROOT_USER "Group_8"

void error(char* s){
	printf("[ERROR] :: %s\n", s);
	return;
}

void debug(char* s){
	printf("[DEBUG] :: %s\n", s);
	return;
}

typedef struct di 		//	This is a data structure to store the directory data
{
	char file_name[30];
	short int inode_no;

	void update(char* name,int inode){
		strcpy(file_name,name);
		inode_no=inode;
	}	
}directory;

typedef struct data_
{
	char data[BLOCK_SIZE];	//	data in the data blocks	
}data_b;

typedef struct block_
{
	char data[BLOCK_SIZE];	//	Just block pointer	
}block;


typedef struct poi_ 		//	This is a data structure for file_open_info
{
	bool allocated;			//	check whether this file_info has any file opened or not;
	char mode;				//	r or w
	int offset;				//	position
	int inode_no;			//	inode of the file

	void ini(char mode_,int a){
		mode=mode_;
		offset=0;
		inode_no=a;
		allocated=true;
	}
	void close(){
		allocated=false;
	}

}file_info;

typedef struct temp_{		//	1+4+8+8+(~9)+8*4+4+4 = 70 bytes (approx)
	bool file_type;		
	int file_size;
	time_t time_l_m;
	time_t time_l_r;
	bitset<9> access_p;
	int direct[8];
	int indirect;
	int double_indirect;

	void ini(){
		for(int i=0;i<9;i++){
			direct[i]=-1;	//	Not pointing to any data block
		}
		indirect=-1;
		double_indirect=-1;
		file_size=0;
		time_l_r=time(NULL);
		time_l_m=time(NULL);
	}

	void create(int file_type_,bitset<9> &access_p_){	//	Creation of inode for a file or directory
		file_type=file_type_;
		time_l_r=time(NULL);
		time_l_m=time(NULL);
		access_p=access_p_;
	}
	void update(int file_size_){		//	if any data is added or any how the file is changed
		file_size+=file_size_;
		time_l_r=time(NULL);
		time_l_m=time(NULL);
	}

	string getpermission(){				//	Return the permission of file in string form
		string s="_";
		for(int i=8;i>=0;i--){
			if(i%3==2){
				if(access_p[i]==0)
					s+="_";
				else
					s+="r";
			}
			else if(i%3==1){
				if(access_p[i]==0)
					s+="_";
				else
					s+="w";
			}
			else{
				if(access_p[i]==0)
					s+="_";
				else
					s+="x";
			}
		}
		return s;
	}

}inode;
	
typedef struct supe_		//	I assume that this will fit in a single block
{
	int total_size;
	int max_inode;
	int used_inode;
	int max_data;
	int used_data;
	int root_dir;			//	stores the root directory
	pthread_mutex_t mutex;
	bitset<MAX_NO_INODE> inodes;
	bitset<MAX_DATA_BLOCKS> data;

	void ini(int max_inode_,int max_data_){
		max_inode=max_inode_;
		max_data=max_data_;
		used_data=0;
		used_inode=0;
		root_dir=0;			//	Root direcotory will always be in inode 0
	}

	int available_inode(){	//	Return the postion available to make file or -1 if full
		if(used_inode==max_inode)
			return -1;
		int pos=0;
		for(int i=0;i<max_inode;i++){
			if(inodes[i]==0){
				pos=i;
				break;
			}
		}
		return pos;
	}


	int available_data(){	//	Return the postion of data block available or -1 if full
		if(used_data==max_data)
			return -1;
		int pos=0;
		for(int i=0;i<max_data;i++){
			if(data[i]==0){
				pos=i;
				break;
			}
		}
		return pos;
	}

	void update_inode(int pos,int type){	//	make inode at pos
		if(type==0){
			inodes[pos]=1;
			used_inode++;
		}
		else{
			inodes[pos]=0;
			used_inode--;
		}
	}

	void update_data(int pos,int type){		//	update data usage
		if(type==0){
			data[pos]=1;
			used_data++;
		}
		else{
			data[pos]=0;
			used_data--;
		}
	}

}super_block;

/*
	create_myfs(int size,int case,int id)
	size -- size of the memory file system
	case -- except 0 or 2	create new memory by malloc , id will not be used and need not be provided
	case -- 1	create new memory by shared memory segment and return its id, id will not be used and need not be provided
	case -- 2   just attach the memory by id number
*/

typedef struct file_
{
	block* B;			//	Block List
	super_block* S;		//	Super Block
	inode* I;			//	Inode list
	data_b* D;			//	Data Block list

	vector<file_info>file_descriptor;		//	File discriptor table

	int create_file(int size, int case1 ,int id){	//	size of memory to make in MB
		long long int n=size*1024*1024;

		if(n==0){
			error("Sorry zero size not possible");
			return -1;
		}

		int shmid=0;
		if(case1 == 1){
			shmid = shmget(IPC_PRIVATE, n, 0666|IPC_CREAT);
			if(shmid == -1){
				error("Shared Memery not created");
				return -1;
			}
			B=(block*)shmat(shmid,0,0);
			S=(super_block*)B;
			pthread_mutexattr_t mattr;
			pthread_mutexattr_init(&mattr);
			pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
			pthread_mutex_init(&(S->mutex), &mattr);
		}
		else if(case1 == 2){
			B=(block*)shmat(id,0,0);
		}
		else{
			B=(block*)malloc(n);
		}

		if(B==NULL){		//	To check if the memory was successfully made
			error("Sorry size too large");
			return	-1;
		}

		S=(super_block*)B;
		I=(inode*)(B+1);

		int no_inodes=0;

		/*
			Deciding on no of inodes 
			Algorithm:
				if we can alot max no of indoes , with every inode having file of .5 mb approx then we alot max inodes 
				else we put no of inodes 10% of space available
		*/

		if(MAX_NO_INODE*1024*512+(MAX_NO_INODE*sizeof(inode))>n-BLOCK_SIZE){		//	approximate bound
			no_inodes=MAX_NO_INODE;
		}
		else{
			no_inodes=((n*.1)/sizeof(inode));
		}

		int temp=ceil(no_inodes*1.0*sizeof(inode)/BLOCK_SIZE);
		D=(data_b*)(B+1+temp);

		//	Initializing structures

		for(int i=0;i<no_inodes;i++){
			I[i].ini();
		}

		S->ini(no_inodes,(n/BLOCK_SIZE)-(temp+1));

		S->total_size=size*1024*1024;													//	Updaing total size of the memory resident file system
		return shmid;
	}

}MYFS;

MYFS* myfs;		//	Memory Resident Unix-like File System
int CURRENT_DIR=0;	//	Current Directory

//	Helper Funcitons

int first_available_index_file_des_table(){			//	Tell the first available index in file discriptor table
	for(int i=0;i<myfs->file_descriptor.size();i++){
		if(myfs->file_descriptor[i].allocated==false){
			return i;
		}
	}
	int n=myfs->file_descriptor.size();
	myfs->file_descriptor.resize(n+1);
	return n;
}
bool check_if_already_open(int inode){				//	Tell if the file is already open
	for(int i=0;i<myfs->file_descriptor.size();i++){
		if(myfs->file_descriptor[i].allocated==true && myfs->file_descriptor[i].inode_no==inode){
			return true;
		}
	}
	return false;
}

int available_data_block (int inode_no){		//	Check if we have space in this inode
	
	inode* I=&(myfs->I[inode_no]);

	int n=ceil(((I->file_size+1)*1.0)/BLOCK_SIZE);

	if(I->file_size==BLOCK_SIZE*8+BLOCK_SIZE*(BLOCK_SIZE/4)+BLOCK_SIZE*(BLOCK_SIZE/4)*(BLOCK_SIZE/4)){
		error("Space not available");
		return -1;
	}

	int add;	//	Data block position where to add the directory

	if(I->file_size%BLOCK_SIZE!=0){
		if(n<=8){
			int d=I->direct[n-1];
			add=d;
		}
		else if(n<=8+(BLOCK_SIZE/4)){
			n-=8;
			int * temp=(int*)&(myfs->D[I->indirect]);
			int d=temp[n-1];
			add=d;
		}
		else{
			n-=(8+BLOCK_SIZE/4);
			int temp=(n-1)*1.0/(BLOCK_SIZE/4);
			int *temp1=(int*)&(myfs->D[I->double_indirect]);
			temp1=(int*)&(myfs->D[temp1[temp]]);
			n-=(temp*(BLOCK_SIZE/4));
			temp1=(int*)&(myfs->D[temp1[n]]);
			add=temp1[0];

		}
	}
	else{
		int b;
		if((b=myfs->S->available_data())==-1){
			error("Space not enough");
			return -1;
		}
		myfs->S->update_data(b,0);

		if(n<=8){
			I->direct[n-1]=b;
		}
		else if(n==9){

			int c;
			if((c=myfs->S->available_data())==-1){
				myfs->S->update_data(b,1);
				error("Space not enough");
				return -1;
			}
			myfs->S->update_data(c,0);
			I->indirect=c;
			int * temp=(int*)&(myfs->D[I->indirect]);
			temp[0]=b;
		}
		else if(n<=8+(BLOCK_SIZE/4)){
			n-=8;
			int * temp=(int*)&(myfs->D[I->indirect]);
			temp[n-1]=b;
		}
		else if(n==8+(BLOCK_SIZE/4)+1){
			int c;
			if((c=myfs->S->available_data())==-1){
				myfs->S->update_data(b,1);
				error("Space not enough");
				return -1;
			}
			myfs->S->update_data(c,0);
			int d;
			if((d=myfs->S->available_data())==-1){
				myfs->S->update_data(b,1);
				myfs->S->update_data(c,1);
				error("Space not enough");
				return -1;
			}
			myfs->S->update_data(d,0);
			I->double_indirect=c;
			int* temp0=(int*)&(myfs->D[c]);
			temp0[0]=d;
			temp0=(int*)&(myfs->D[d]);
			temp0[0]=b;
		}
		else{
			n-=(8+BLOCK_SIZE/4);
			int temp=(n-1)/(BLOCK_SIZE/4);
			int *temp1=(int*)&(myfs->D[I->double_indirect]);
			if(temp1[temp]==-1){
				int c;
				if((c=myfs->S->available_data())==-1){
					myfs->S->update_data(b,1);
					error("Space not enough");
					return -1;
				}
				myfs->S->update_data(c,0);
				int * help=(int*)&(myfs->D[temp1[temp]]);
				help[0]=c;
				help=(int*)&(myfs->D[c]);
				help[0]=b;
			}
			temp1=(int*)&(myfs->D[temp1[temp]]);
			n-=(temp*(BLOCK_SIZE/4));
			temp1[n-1]=b; 

		}
		add=b;
	}

	return add;
}

int check_direct_file_exist(char* filename,int data,int &filesize,bool print,bool extract,vector<directory>&V){
	directory* dd=(directory*)&(myfs->D[data]);
	for(int i=0;i<BLOCK_SIZE/sizeof(directory)&&filesize!=0;i++){
		if(extract==false&&print==false&&strcmp(dd[i].file_name,filename)==0){
			return dd[i].inode_no;
		}
		else if(print==true){
			string s=myfs->I[dd[i].inode_no].getpermission();
			cout<<s<<"   1   ";
			cout<<ROOT_USER<<"   ";
			cout<<ROOT_USER<<"   ";
			cout<<myfs->I[dd[i].inode_no].file_size<<"   ";
			char buff[20];
			struct tm * timeinfo;
			timeinfo = localtime (&(myfs->I[dd[i].inode_no].time_l_m));
			strftime(buff, sizeof(buff), "%b %d %H:%M", timeinfo);
			printf("%s   ",buff);
			cout<<dd[i].file_name<<endl;
		}
		if(extract==true){
			V.push_back(dd[i]);
		}
		filesize-=sizeof(directory);
	}
	return -1;
}

int check_indirect_file_exist(char* filename,int data,int &filesize,bool print,bool extract,vector<directory>&V){
	int * temp=(int*)&(myfs->D[data]);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		if(filesize==0)
			return -1;
		int temp1;
		if((temp1=check_direct_file_exist(filename,temp[i],filesize,print,extract,V))==-1 && filesize==0)
			return -1;
		else if(temp1!=-1)
			return temp1;
	}
}

int Check_file_exist(char* filename,bool print,bool extract,vector<directory>&V){		//	Check if file exists in current directory && also designed to print all the files in directory
	inode* I=(inode*)&(myfs->I[CURRENT_DIR]);
	int filesize=I->file_size;
	for(int i=0;i<8;i++){
		if(filesize==0){
			return -1;
		}
		int temp;
		if((temp=check_direct_file_exist(filename,I->direct[i],filesize,print,extract,V))==-1 && filesize==0)
			return -1;
		else if(temp!=-1)
			return temp;
	}
	int tem;
	if((tem=check_indirect_file_exist(filename,I->indirect,filesize,print,extract,V))==-1 && filesize==0){
		return -1;
	}
	else if(tem!=-1)
		return tem;

	int* temp1=(int*)&(myfs->D[I->double_indirect]);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		if(filesize==0)
			return -1;
		int temp;
		if((temp=check_indirect_file_exist(filename,temp1[i],filesize,print,extract,V))==-1 && filesize==0)
			return -1;
		else if(temp!=-1)
			return temp;
	}
	return -1;
}


void Show_direct_file_helper(int data,int &filesize,bool free){
	if(free){
		myfs->S->update_data(data,1);
		if(filesize<BLOCK_SIZE){
			filesize=0;
		}
		else{
			filesize-=BLOCK_SIZE;
		}
		return;
	}
	// cout<<"BLOCK NO "<<data<<endl;
	data_b * B=(data_b*)&(myfs->D[data]);
	char sh[BLOCK_SIZE];
	if(filesize<BLOCK_SIZE){
		strncpy(sh,B->data,filesize);
		printf("%.*s", filesize,  sh);
		filesize=0;
	}
	else{
		filesize-=BLOCK_SIZE;
		strncpy(sh,B->data,BLOCK_SIZE);
		printf("%.*s", BLOCK_SIZE,  sh);
	}
	
	// cout<<"\nERROR\n";
	// cout<<strlen(sh)<<endl;
	return ;
}

void Show_indirect_file_helper(int data,int &filesize,bool free){
	int * temp=(int*)&(myfs->D[data]);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		Show_direct_file_helper(temp[i],filesize,free);
		if(filesize==0)
			return ;
	}
}

int Show_file_helper(int inode1,bool free){		//	Show the whole data of the file or remove the file
	inode* I=(inode*)&(myfs->I[inode1]);
	int filesize=I->file_size;
	if(filesize==0)
		return 0;
	for(int i=0;i<8&&filesize!=0;i++){
		// cout<<"inode1 	"<<inode1<<"  "<<I->direct[i]<<endl;
		Show_direct_file_helper(I->direct[i],filesize,free);
		if(free){
			I->direct[i]=-1;
		}
		if(filesize==0)
			return 0;
	}
	Show_indirect_file_helper(I->indirect,filesize,free);
	if(free){
		myfs->S->update_data(I->indirect,1);
		I->indirect=-1;
	}
	if(filesize==0)
		return 0;
	int* temp1=(int*)&(myfs->D[I->double_indirect]);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		Show_indirect_file_helper(temp1[i],filesize,free);
		if(free){
			myfs->S->update_data(temp1[i],1);
			I->double_indirect=-1;
		}
		if(filesize==0)
			return 0;
	}
	return 0;
}


void read_direct_file_helper(string &S,int &pos,int &bytes,int data,int &cpos){
	// cout<<"READ DATA BLOCK -----------------------------"<<data<<endl;
	data_b * B=(data_b*)&(myfs->D[data]);
	for(int i=0;i<BLOCK_SIZE&&bytes!=0;i++){
		if(cpos>=pos){
			S+=string(1,B->data[i]);
			// cout<<B->data[i];
			bytes--;
			pos++;
		}
		cpos++;
	}
	return ;
}

void read_indirect_file_helper(string &S,int &pos,int &bytes,int data,int &cpos){
	int * temp=(int*)&(myfs->D[data]);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		read_direct_file_helper(S,pos,bytes,temp[i],cpos);
		if(bytes==0)
			return ;
	}
}

int read_file_helper(string &S,int &pos,int &bytes,int inode1){		//	read based on the bool provided, S is the string which it will use to read
	inode* I=(inode*)&(myfs->I[inode1]);
	int cpos=0;
	if(bytes==0)
		return 0;
	for(int i=0;i<8&&bytes!=0;i++){
		read_direct_file_helper(S,pos,bytes,I->direct[i],cpos);
		if(bytes==0)
			return 0;
	}
	read_indirect_file_helper(S,pos,bytes,I->indirect,cpos);
	if(bytes==0)
		return 0;
	int* temp1=(int*)&(myfs->D[I->double_indirect]);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		read_indirect_file_helper(S,pos,bytes,temp1[i],cpos);
		if(bytes==0)
			return 0;
	}
	return 0;
}

void write_direct_file_helper(string &S,int pos,int &bytes,int data,int &cpos){
	data_b * B=(data_b*)&(myfs->D[data]);
	for(int i=0;i<BLOCK_SIZE&&bytes!=0;i++){
		if(cpos>=pos){
			B->data[i]=S[cpos-pos];
			bytes--;
		}
		cpos++;
	}
	return ;
}

int write_indirect_file_helper(string &S,int pos,int &bytes,int data,int &cpos,int filesize){
	int * temp=(int*)&(myfs->D[data]);
	int n=filesize;
	n=max(n,0);
	n=ceil(n*1.0/BLOCK_SIZE);
	for(int i=0;i<BLOCK_SIZE/4;i++){
		if(n==0){
			int add=myfs->S->available_data();
			if(add==-1){
				error("Space not enough to write");
				return -1;
			}
			myfs->S->update_data(add,0);
			temp[i]=add;
		}
		else
			n--;
		write_direct_file_helper(S,pos,bytes,temp[i],cpos);
		if(bytes==0)
			return 0;
	}
}

int write_file_helper(string &S,int pos,int &bytes,int inode1){		//	write based on the bool provided, S is the string which it will use to write
	inode* I=(inode*)&(myfs->I[inode1]);
	int filesize=I->file_size;
	int cpos=0;
	if(bytes==0)
		return 0;
	for(int i=0;i<8&&bytes!=0;i++){
		if(I->direct[i]==-1){
			int add=myfs->S->available_data();
			if(add==-1){
				error("Space not enough to write");
				return -1;
			}
			myfs->S->update_data(add,0);
			I->direct[i]=add;
		}
		write_direct_file_helper(S,pos,bytes,I->direct[i],cpos);
		if(bytes==0)
			return 0;
	}
	if(I->indirect==-1){
		int add=myfs->S->available_data();
		if(add==-1){
			error("Space not enough to write");
			return -1;
		}
		myfs->S->update_data(add,0);
		int add1=myfs->S->available_data();
		if(add1==-1){
			myfs->S->update_data(add,1);
			error("Space not enough to write");
			return -1;
		}
		I->indirect==add;
	}
	filesize-=8*BLOCK_SIZE;
	int temp=write_indirect_file_helper(S,pos,bytes,I->indirect,cpos,filesize);
	if(temp==-1)
		return -1;
	if(bytes==0)
		return 0;
	filesize-=(BLOCK_SIZE*BLOCK_SIZE/4);
	if(I->double_indirect==-1){
		if(myfs->S->max_data-myfs->S->used_data<3){
			error("Space not enough to write");
			return -1;
		}
		I->double_indirect=myfs->S->available_data();
		myfs->S->update_data(I->double_indirect,0);
	}
	int* temp1=(int*)&(myfs->D[I->double_indirect]);
	int n=filesize;
	n=max(n,0);
	n=ceil(n*1.0/(BLOCK_SIZE*BLOCK_SIZE/4));
	for(int i=0;i<BLOCK_SIZE/4;i++){
		if(n==0){
			int add=myfs->S->available_data();
			if(add==-1){
				error("Space not enough to write");
				return -1;
			}
			myfs->S->update_data(add,0);
			temp1[i]=add;
		}
		else
			n--;
		int t=write_indirect_file_helper(S,pos,bytes,temp1[i],cpos,filesize);
		filesize-=(BLOCK_SIZE*BLOCK_SIZE/4);
		if(t==-1)
			return -1;
		if(bytes==0)
			return 0;
	}
	return 0;
}


//	Functions to create and use Memory Resident Unix-like File System

int mkdir_myfs (char *dirname){	

	pthread_mutex_lock(&(myfs->S->mutex));
	printf("LOCK\n");

	int a;
	if((a=myfs->S->available_inode())==-1){
		error("Inodes not available to make directory");
		return -1;
	}
	
	int add=available_data_block(CURRENT_DIR);
	
	if(add==-1){
		error("Directory creation unsucessfull");
		return -1;
	}
	
	directory* TTA=(directory*)&(myfs->D[add]);
	myfs->S->update_inode(a,0);
	int t=(myfs->I[CURRENT_DIR].file_size%BLOCK_SIZE)/32;
	TTA[t].update(dirname,a);
	string bitstring=PERMISSION;
	bitset<9>alpha(bitstring);
	myfs->I[a].create(1,alpha);
	myfs->I[CURRENT_DIR].update(sizeof(directory));
	debug("Directory successfully created");

	pthread_mutex_unlock(&(myfs->S->mutex));
	printf("UNLOCK\n");
	return 0;
}
int create_myfs(int size,int case1,int id=1){						
	myfs=(MYFS*)malloc(sizeof(MYFS));
	int a;
	if(myfs==NULL || (a=myfs->create_file(size,case1,id))==-1){
		error("MYFS creation unsucessfull");
		return -1;
	}
 	string bitstring=PERMISSION;
	bitset<9>alpha(bitstring);
	myfs->I[0].create(1,alpha);
	myfs->S->update_inode(0,0);
	debug("MYFS successfully created");
	return a;
}  

int copy_pc2myfs (char *source, char *dest){

	pthread_mutex_lock(&(myfs->S->mutex));

	FILE* b;
	if((b=fopen(source,"r"))==NULL)
	{
		error("Source file do not exist");
		return -1;
	}
	fclose(b);
	int a;
	if((a=myfs->S->available_inode())==-1){
		error("Inodes not available to make file");
		return -1;
	}

	int add=available_data_block(CURRENT_DIR);
	if(add==-1){
		error("File creation unsucessfull");
		return -1;
	}
	directory* TTA=(directory*)&(myfs->D[add]);
	myfs->S->update_inode(a,0);
	int t=(myfs->I[CURRENT_DIR].file_size%BLOCK_SIZE)/32;
	TTA[t].update(dest,a);
	string bitstring=PERMISSION;
	bitset<9>alpha(bitstring);
	myfs->I[a].create(0,alpha);
	myfs->I[CURRENT_DIR].update(sizeof(directory));
	debug("File successfully created");
	struct stat file_stat;
	int fd=open(source,O_RDONLY);
	/* Get file stats */
    if (fstat(fd, &file_stat) < 0)
    {
        error("Error fstat ");
        return -1;
    }
    int filesize=file_stat.st_size;
    if(filesize==0){
    	debug("File successfully copied");
    	return 0;
    }
   	for(int i=0;i<ceil(filesize*1.0/BLOCK_SIZE);i++){
   		add=available_data_block(a);	  		
		if(add==-1){
			error("Partial File copied , data block finished");
			close(fd);
			return -1;
		}
		int temp;
		char C[BLOCK_SIZE];
		bzero(C, BLOCK_SIZE);
		if((temp=read(fd,C,BLOCK_SIZE))==-1){
			error("Partial File copied , error reading file");
			close(fd);
			return -1;
		}
		strncpy((char*)&(myfs->D[add]),C,temp);
		myfs->I[a].update(temp);
   	}

   	close(fd);

   	pthread_mutex_unlock(&(myfs->S->mutex));

   	debug("File successfully copied");

	return 0;
}

int showfile_myfs (char *filename){
	int a;
	vector<directory>V;
	if((a=Check_file_exist(filename,false,false,V))==-1){
		error("File do not exist");
		return -1;
	}
	Show_file_helper(a,false);
	return 0;
}

int ls_myfs (){
	debug("\n\n***********LIST OF FILES************");
	vector<directory>V;
	Check_file_exist("",true,false,V);
	return 0;
}	

int rm_myfs (char *filename){

	pthread_mutex_lock(&(myfs->S->mutex));

	int a;
	vector<directory>V;
	if((a=Check_file_exist(filename,false,false,V))==-1){
		error("File do not exist");
		return -1;
	}
	myfs->S->update_inode(a,1);		//	freeing file inode
	Show_file_helper(a,true);		//	freeing data blocks used in file

	Check_file_exist(filename,false,true,V);		//	extracting files in directory
	Show_file_helper(CURRENT_DIR,true);	//	free data nodes of directory
	myfs->I[CURRENT_DIR].file_size=0;		//	making filesize of diretory 0
	for(int i=0;i<V.size();i++){					//	Filling data of directory
		if(strcmp(filename,V[i].file_name)!=0){
			int add=available_data_block(CURRENT_DIR);			
			if(add==-1){
				error("Due to some error, File system currupted");
				exit(0);
			}
			
			directory* TTA=(directory*)&(myfs->D[add]);
			int t=(myfs->I[CURRENT_DIR].file_size%BLOCK_SIZE)/32;
			TTA[t].update(V[i].file_name,V[i].inode_no);
			myfs->I[CURRENT_DIR].update(sizeof(directory));
		}
	}

	pthread_mutex_unlock(&(myfs->S->mutex));

	debug("File successfully removed");
	return 0;
}

int chdir_myfs (char *dirname){
	int a;
	vector<directory>V;
	if((a=Check_file_exist(dirname,false,false,V))==-1 || myfs->I[a].file_type!=1){
		error("Directory do not exist");
		return -1;
	}
	CURRENT_DIR=a;
	printf("Directory charnged to ## %s\n",dirname );
	return 0;
}

int rmdir_myfs (char *dirname){

	pthread_mutex_lock(&(myfs->S->mutex));

	int a;
	vector<directory>V;
	if((a=Check_file_exist(dirname,false,false,V))==-1 || myfs->I[a].file_type!=1){
		error("Directory do not exist");
		return -1;
	}
	int current=CURRENT_DIR;		//	checking current directroy
	chdir_myfs(dirname);					//	going to that directory
	Check_file_exist("",false,true,V);		//	extracting files in directory
	for(int i=0;i<V.size();i++){					//	Filling data of directory
		if(myfs->I[V[i].inode_no].file_type==0){
			myfs->S->update_inode(V[i].inode_no,1);		//	freeing file inode
			Show_file_helper(V[i].inode_no,true);		//	freeing data blocks used in file
		}
		else{
			rmdir_myfs(V[i].file_name);
		}
	}
	CURRENT_DIR=current;			//	comming back to our directory
	rm_myfs(dirname);						//	Removing the directory data

	pthread_mutex_unlock(&(myfs->S->mutex));
	return 0;
}

int open_myfs (char *filename, char mode,bool iscreate){
	int a;
	vector<directory>V;
	if((a=Check_file_exist(filename,false,false,V))==-1 || myfs->I[a].file_type!=0){
		if(!iscreate){
			error("File do not exist");
			return -1;
		}

		pthread_mutex_lock(&(myfs->S->mutex));

		int b;
		if((b=myfs->S->available_inode())==-1){
			error("Inodes not available to make file in open");
			return -1;
		}
		
		int add=available_data_block(CURRENT_DIR);
		
		if(add==-1){
			error("File creation unsucessfull");
			return -1;
		}
		directory* TTA=(directory*)&(myfs->D[add]);
		myfs->S->update_inode(b,0);
		int t=(myfs->I[CURRENT_DIR].file_size%BLOCK_SIZE)/32;
		TTA[t].update(filename,b);
		string bitstring=PERMISSION;
		bitset<9>alpha(bitstring);
		myfs->I[b].create(0,alpha);
		myfs->I[CURRENT_DIR].update(sizeof(directory));
		debug("File successfully created");
		a=b;

		pthread_mutex_unlock(&(myfs->S->mutex));

	}
	if(mode!='r'&& mode!='w'){
		error("Wrong mode provided");
		return -1;
	}
	if(check_if_already_open(a)){
		error("Requested file is already opened.First close that instance");
		return -1;
	}
	int fd=first_available_index_file_des_table();
	myfs->file_descriptor[fd].ini(mode,a);
	debug("File successfully opened");
	return fd;
}

int close_myfs(int fd){

	if(fd>=myfs->file_descriptor.size() || myfs->file_descriptor[fd].allocated==false){
		return -1;
	}
	myfs->file_descriptor[fd].allocated=false;

	return 0;
}


int read_myfs (int fd, int nbytes, char *buff){
	if(fd<0||fd>=myfs->file_descriptor.size()||myfs->file_descriptor[fd].allocated==false || myfs->S->inodes[myfs->file_descriptor[fd].inode_no]==0){
		error("Wrong file descripter");
		return -1;
	}
	int a=myfs->file_descriptor[fd].inode_no;
	inode* I=(inode*)&(myfs->I[a]);
	int &offset=myfs->file_descriptor[fd].offset;		//	Reference variable to change file offset

	int ret_bytes=0;									//	bytes to be readed
	if(offset>=I->file_size){
		buff=NULL;
		return 0;
	}
	if(offset+nbytes>=I->file_size){
		ret_bytes=I->file_size-offset;
	}
	else{
		ret_bytes=nbytes;
	}

	string S="";
	int temp1=ret_bytes;
	read_file_helper(S,offset,temp1,a);
	char * temp=(char*)S.c_str();
	strncpy(buff,temp,ret_bytes+1);
	debug("File successfully read");
	return ret_bytes;
}

int write_myfs(int fd, int nbytes, char *buff){

	pthread_mutex_lock(&(myfs->S->mutex));

	if(fd<0||fd>=myfs->file_descriptor.size()||myfs->file_descriptor[fd].allocated==false || myfs->S->inodes[myfs->file_descriptor[fd].inode_no]==0){
		error("Wrong file descripter");
		return -1;
	}
	int a=myfs->file_descriptor[fd].inode_no;
	inode* I=(inode*)&(myfs->I[a]);
	int &offset=myfs->file_descriptor[fd].offset;		//	Reference variable to change file offset

	int ret_bytes=nbytes;									//	bytes to be writed
	string S=string(buff);
	if(S.length()<nbytes){
		nbytes=S.length();
		ret_bytes=nbytes;
	}
	write_file_helper(S,offset,nbytes,a);
	if(myfs->I[a].file_size<=offset+ret_bytes)
		myfs->I[a].update(offset+ret_bytes-myfs->I[a].file_size);
	debug("File successfully written");
	offset+=ret_bytes;

	pthread_mutex_unlock(&(myfs->S->mutex));

	return ret_bytes;
}

int eof_myfs (int fd){
	if(fd<0||fd>=myfs->file_descriptor.size()||myfs->file_descriptor[fd].allocated==false || myfs->S->inodes[myfs->file_descriptor[fd].inode_no]==0){
		return -1;
	}
	int a=myfs->file_descriptor[fd].inode_no;
	inode* I=(inode*)&(myfs->I[a]);
	int &offset=myfs->file_descriptor[fd].offset;		//	Reference variable to change file offset
	if(offset==I->file_size)
		return 1;
	return 0;
}

int dump_myfs (char *dumpfile){
	int fp=open(dumpfile,O_CREAT | O_RDWR ,S_IRWXU);
	if(fp==-1)
		return -1;
	int a;
	if((a=write(fp,(const void*)myfs->B,myfs->S->total_size))!=myfs->S->total_size)
		return -1;
	close(fp);
	debug("Database successfully dumped");
	return 0;
}

int restore_myfs(char* dumbfile){
	FILE* b;
	if((b=fopen(dumbfile,"r"))==NULL)
	{
		error("Source file do not exist");
		return -1;
	}
	fclose(b);
	int fp=open(dumbfile,O_CREAT | O_RDWR,S_IRWXU);
	if(fp==-1)
		return -1;
	int a;
	struct stat file_details;
	stat(dumbfile,&file_details);
	int sizz=file_details.st_size;
	void * TEMP=(void*)malloc(sizz);
	if((a=read(fp,(void*)TEMP,sizz))!=sizz)
		return -1;
	myfs=(MYFS*)malloc(sizeof(MYFS));
	if(myfs==NULL)
		return -1;
	myfs->B=(block*)TEMP;
	myfs->S=(super_block*)(TEMP);
	myfs->I=(inode*)(myfs->B+1);
	int t=myfs->S->max_inode;
	int t1=ceil((t*sizeof(inode)*1.0)/BLOCK_SIZE);
	myfs->D=(data_b*)(myfs->B+t1+1);
	debug("File system successfully restored");
	return 0;
}

int status_myfs(){
	printf("Total size of file system : %d\n",myfs->S->total_size);
	printf("Maximum inode : %d\n",myfs->S->max_inode );
	printf("Used inode : %d\n",myfs->S->used_inode );
	printf("Maximum data blocks : %d\n",myfs->S->max_data );
	printf("Used data blocks : %d\n",myfs->S->used_data );
	printf("Root directory : %d\n",myfs->S->root_dir );
	printf("Current directory : %d\n",CURRENT_DIR );
	return -1;
}

int chmod_myfs(char*name,int mode){
	int a;
	vector<directory>V;
	if((a=Check_file_exist(name,false,false,V))==-1 || myfs->I[a].file_type!=0){
		error("File do not exist");
		return -1;
	}

	string s=to_string(mode);
	bitset<9>alpha(s);
	myfs->I[a].create(myfs->I[a].file_type,alpha);
	return 0;
	
}

int copy_myfs2pc(char* source ,char* dest){
	int a;
	vector<directory>V;
	if((a=Check_file_exist(source,false,false,V))==-1 || myfs->I[a].file_type!=0){
		error("File do not exist");
		return -1;
	}
	int fp=open(dest,O_CREAT | O_RDWR ,S_IRWXU);
	if(fp==-1)
		return -1;
	int fpp=open_myfs(source,'r',false);
	int file_size=myfs->I[a].file_size;
	char* buff=(char*)malloc(file_size);
	read_myfs(fpp,file_size,buff);
	write(fp,buff,file_size);
	close(fp);
	close_myfs(fpp);
	return 0;
}

int cd_root(){
	CURRENT_DIR=0;
	return 0;
}