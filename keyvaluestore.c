#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <asm/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define DEBUG_MODE_1
#define  MAX_NUMBER_OF_KEY_VALUE_PAIRS 2048


pthread_cond_t empty, fill;
pthread_mutex_t mutex;
int done = 0;
int transactionid = 0;



static __u32 hash(__u64 key)
{
	int temp;
	temp = key;
	temp >>= 3;
	return (key ^ (temp>>10) ^ (temp>>20)) & 0xBFF;
}

struct keyvalue_base
{
	__u64 key;
	__u64 size;
	char data[1024];
	// replace with char* data
	// dont forget to allocate memory to data and copy cmd->kventry->data into it
};
typedef struct keyvalue_base keyvalue_set;
typedef struct keyvalue_base keyvalue_get;

struct dictnode 
{
	struct dictnode* next;
	keyvalue_set* kventry;
	__u64 size; 
};

struct dictionary
{
	struct dictnode* hashmap[MAX_NUMBER_OF_KEY_VALUE_PAIRS];
};

struct dictionary* map = NULL;


static void initializeDictionary()
{
	map = (struct dictionary*)malloc(sizeof(struct dictionary));	
	if(map == NULL)
	{
		fprintf(stderr," Memory allocation to map failed!");
		exit(0);
	}
	int i = 0;
	for(i=0; i< MAX_NUMBER_OF_KEY_VALUE_PAIRS; i++)
	{
		map->hashmap[i]	= NULL;
	}
}

static long get_keyvalue(keyvalue_get* cmd)
{
	int key_to_be_fetched	= cmd->key;
	struct dictnode* val	= map->hashmap[key_to_be_fetched];

	if(val == NULL)
	{
		#ifdef DEBUG_MODE_1
			fprintf(stdout,"\tRequested key %llu is yet to be inserted in the map", cmd->key);
		#endif
		return -1;
	}
	else
	{
		while(val != NULL)
		{
			if(val->kventry->key	== cmd->key)
			{
				return 1;
			}
			val	= val->next;
		}
		
		return -1;
	}

}


static long set_keyvalue( keyvalue_set* cmd )
{
	
	int key_to_be_set = cmd->key;
	struct dictnode* val = map->hashmap[key_to_be_set];
	
	if(val == NULL)
	{
	
		struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
		if(head == NULL)
		{
			fprintf(stderr," Memory allocation to node in dictionary failed!");
			exit(0);
		}
		head->kventry = cmd;
		head->size = cmd->size;
		head->next = NULL;
	
		map->hashmap[key_to_be_set] = head;
		return 1;

	}
	else
	{	
		if(val->next == NULL)
		{	
			if(val->kventry->key == key_to_be_set)
			{	
				strcpy(val->kventry->data, (cmd)->data);
			}
			else
			{	
				struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
				if(head == NULL)
				{
					fprintf(stderr," Memory allocation to node in dictionary failed!");
					exit(0);
				}
				head->kventry = cmd;
				head->size = cmd->size;
				head->next = NULL;
				
				val->next = head;
			}
		}
		else
		{
			while(val->next != NULL)
			{	
				if(val->kventry->key == key_to_be_set)
				{
					strcpy(val->kventry->data,cmd->data);
					return 1;
				}								
				val = val->next;	
			}

			struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
			if(head == NULL)
			{
				fprintf(stderr," Memory allocation to node in dictionary failed!");
				exit(0);
			}
			head->kventry = cmd;
			head->size = cmd->size;
			head->next = NULL;
		
			val->next = head;
		}
		return 1;
	}

	return 0;

}

void* consumer()
{
	struct dictnode* start = NULL;
	int entries = (int)(sizeof(map->hashmap))/(sizeof(struct dictnode*));
	int i;

	for(i=0; i<entries; i++)
	{
		start = map->hashmap[i];
		
		if(start != NULL)
		{
			printf("start: %llu\n", start->kventry->key);						
			if(start->next == NULL)
			{
				printf("\nThe key is: %llu \n", start->kventry->key);
				printf("Data: %s \n\r", start->kventry->data);
			}
			else
			{
				while(start != NULL)
				{	
					printf("\nThe key is: %llu \n", start->kventry->key);
					printf("Data: %s \n\r", start->kventry->data);
					start = start->next;
				}
			}	
		}		
	}
	
}


long kv_set(__u64 key, __u64 size, void *data)
{
     	keyvalue_set * cmd = (keyvalue_set *) malloc(sizeof(keyvalue_set));
     	cmd->key = key;     
     	cmd->size = size;
     	strcpy(cmd->data, data);	
     	//return set_keyvalue(&cmd);
     	return set_keyvalue(cmd);
}


void* producer()
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
		tid = kv_set(i, strlen(data), data);
        	fprintf(stdout,"S\t%d\t%d\t%zu\t%s\n",tid,i,strlen(data),data);
    	}	
	

}

int main()
{
	printf("Begin\n");
	pthread_t p;
	pthread_t c;

	pthread_create(&p, NULL, producer, NULL);
	pthread_join(p, NULL);
	printf("\n");
	pthread_create(&p, NULL, consumer, NULL);
	pthread_join(p, NULL);

	printf("\nEnd\n");

	return 0;

}
