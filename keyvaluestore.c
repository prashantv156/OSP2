#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <asm/types.h>

int  MAX_NUMBER_OF_KEY_VALUE_PAIRS = 2048;

static __u32 hash(__u64 key)
{
	int temp;
	temp = key;
	temp >>= 3;
	return (key ^ (temp>>10) ^ (temp>>20)) & 0xBFF;
}

struct keyvalue_set
{
	__u64 key;
	__u64 size;
	char data[1024];
};

struct dictnode 
{
	struct dictnode* next;
	struct keyvalue_set* kventry;
	__u64 size; 
};

struct dictionary
{
	struct dictnode* hashmap[2048];
};

struct dictionary* map = NULL;


static void initializeDictionary()
{
	map = (struct dictionary*)malloc(sizeof(struct dictionary));	
}


static long keyvalue_set(struct keyvalue_set** cmd)
{
	
	int key_to_be_set = (*cmd)->key;

	struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
	head->kventry = *cmd;
	head->size = (*cmd)->size;
	head->next = NULL;

	struct dictnode* val = map->hashmap[key_to_be_set];
	
	printf("val: %llu\n", val);
	
	if(val == 0)
	{
		map->hashmap[key_to_be_set] = head;
		return 1;
	}
	else
	{	
		if(val->next == NULL)
		{	
			val->next = head;
		}
		else
		{
			while(val->next != NULL)
			{	
				val = val->next;
			}
		
			val->next = head;
			
		}
		return 1;
	}

	return 0;

}

void printAllKeys()
{
	struct dictnode* start = NULL;
	int entries = (int)(sizeof(map->hashmap))/(sizeof(struct dictnode*));
	int i;

	for(i=0; i<entries; i++)
	{
		start = map->hashmap[i];
		
		if(start != 0)
		{
			printf("start: %llu\n", start);
						
			if(start->next == NULL)
			{
				printf("The key is: %llu \n", start->kventry->key);
				printf("Data: %s \n", start->kventry->data);
			}
			else
			{
				while(start != NULL)
				{	
					printf("The key is: %llu \n", start->kventry->key);
					printf("Data: %s \n", start->kventry->data);
					start = start->next;
				}
			}	
		}		
	}
	
}


long kv_set(__u64 key, __u64 size, void *data)
{
     	struct keyvalue_set * cmd = (struct keyvalue_set *) malloc(sizeof(struct keyvalue_set));
     	cmd->key = key;     
     	cmd->size = size;
     	strcpy(cmd->data, data);	
     	return keyvalue_set(&cmd);
}


int main()
{	

	int number_of_keys = 20;	
	int a, i;
	int tid;
	__u64 size;
	__u64 key;
	initializeDictionary();	
	srand((int)time(NULL)+(int)getpid());
	char data[1024];	
	
	for(i = 0; i < number_of_keys; i++)
    	{
        	memset(data, 0, 1024);
        	a = rand();
       		sprintf(data,"%d",a);
		if(i%2 == 0)
		{
        		tid = kv_set(i,strlen(data),data);
        		fprintf(stderr,"S\t%d\t%d\t%d\t%s\n",tid,i,strlen(data),data);
		}
		tid = kv_set(i, strlen(data), data);
        	fprintf(stderr,"S\t%d\t%d\t%d\t%s\n",tid,i,strlen(data),data);
    	}
	
	printf("%s", "\nPrinting all stored keys\n");
	printAllKeys();
	return 0;

}


