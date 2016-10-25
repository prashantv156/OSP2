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

int done = 0;
int transactionid = 0;


//////////////////////////// READERS WRITERS LOCK ///////////////////////////////////

typedef struct _rwlock_t
{
	sem_t lock; // binary semaphore (basic lock)
	sem_t writelock; // used to allow ONE writer or MANY readers
	int readers; // count of readers reading in critical section
} rwlock_t;

void rwlock_init(rwlock_t *rw)
{
	rw->readers = 0;
	sem_init(&rw->lock, 0, 1);
	sem_init(&rw->writelock, 0, 1);
}


void rwlock_acquire_readlock(rwlock_t *rw) 
{
	sem_wait(&rw->lock);
	rw->readers++;
	if (rw->readers == 1)
		sem_wait(&rw->writelock); // first reader acquires writelock
	sem_post(&rw->lock);
}

void rwlock_release_readlock(rwlock_t *rw) 
{
	sem_wait(&rw->lock);
	rw->readers--;
	if (rw->readers == 0)
		sem_post(&rw->writelock); // last reader releases writelock
	sem_post(&rw->lock);
}


void rwlock_acquire_writelock(rwlock_t *rw)
{
	sem_wait(&rw->writelock);
}
 
void rwlock_release_writelock(rwlock_t *rw)
{	
	sem_post(&rw->writelock);
}

rwlock_t* lock = NULL;

/////////////////////////////////////////////////////////////////////////////////////

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
typedef struct keyvalue_base keyvalue_delete;

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
	struct dictnode* curr	= map->hashmap[key_to_be_fetched];
	
	rwlock_acquire_readlock(lock);
		
	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			fprintf(stdout,"\tRequested key %llu is yet to be inserted in the map", cmd->key);
		#endif
		rwlock_release_readlock(lock);
		return -1;
	}
	else
	{
		while(curr != NULL)
		{
			if(curr->kventry->key	== cmd->key)
			{
				#ifdef DEBUG_MODE_1
        				fprintf(stdout,"Read:\tkey: %llu\tsize: %llu\tdata: %s\n",curr->kventry->key, curr->size, curr->kventry->data);
				#endif
				rwlock_release_readlock(lock);
				return 1;
			}
			curr	= curr->next;
		}
		
		#ifdef DEBUG_MODE_1
			fprintf(stdout,"\tRequested key %llu is yet to be inserted in the map", cmd->key);
		#endif
		rwlock_release_readlock(lock);
		return -1;
	}

}

static long delete_keyvalue(keyvalue_delete* cmd)
{
	int key_to_be_deleted = cmd->key;
	struct dictnode* curr = map->hashmap[key_to_be_deleted];

	rwlock_acquire_writelock(lock);

	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			fprintf(stdout, "Map is empty. Cannot perform delete operation!");
		#endif
		rwlock_release_writelock(lock);
		return -1;
	}
	else if(curr->kventry->key	== cmd->key)
	{
		map->hashmap[key_to_be_deleted]	= curr->next;
		free(curr);
		rwlock_release_writelock(lock);
		return 1;
	}
	else
	{
		struct dictnode* prev	= curr;
		while(curr != NULL)
		{
			if(curr->kventry->key	== cmd->key)
			{
				prev->next	= curr->next;
				free(curr);
				rwlock_release_writelock(lock);
				return 1;
			}
			prev	= curr;
			curr 	= curr->next;
		}

		#ifdef DEBUG_MODE_1
			fprintf(stdout, "The requested key is non-existant in the map!");
		#endif
		rwlock_release_writelock(lock);
		return -1;
	}
}

static long set_keyvalue( keyvalue_set* cmd )
{
	
	int key_to_be_set = cmd->key;
	struct dictnode* val = map->hashmap[key_to_be_set];
	
	rwlock_acquire_writelock(lock);

	if(val == NULL)
	{
	
		struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
		if(head == NULL)
		{
			fprintf(stderr," Memory allocation to node in dictionary failed!");
			rwlock_release_writelock(lock);
			exit(0);
		}
		head->kventry = cmd;
		head->size = cmd->size;
		head->next = NULL;
	
		map->hashmap[key_to_be_set] = head;
		rwlock_release_writelock(lock);
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
					rwlock_release_writelock(lock);
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
					rwlock_release_writelock(lock);
					return 1;
				}								
				val = val->next;	
			}

			struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
			if(head == NULL)
			{
				fprintf(stderr," Memory allocation to node in dictionary failed!");
				rwlock_release_writelock(lock);
				exit(0);
			}
			head->kventry = cmd;
			head->size = cmd->size;
			head->next = NULL;
		
			val->next = head;
		}
		rwlock_release_writelock(lock);
		return 1;
	}

	return 0;

}
/*
void* consumer(int entries)
{
	struct dictnode* start = NULL;
	//int entries = (int)(sizeof(map->hashmap))/(sizeof(struct dictnode*));
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
*/

long kv_set(__u64 key, __u64 size, void *data)
{
     	keyvalue_set * cmd = (keyvalue_set *) malloc(sizeof(keyvalue_set));
     	cmd->key = key;     
     	cmd->size = size;
     	strcpy(cmd->data, data);	
     	//return set_keyvalue(&cmd);
     	return set_keyvalue(cmd);
}

long kv_get(__u64 key, __u64 size, void *data)
{
     	keyvalue_get * cmd = (keyvalue_get *) malloc(sizeof(keyvalue_get));
     	cmd->key = key;     
     	cmd->size = size;
     	strcpy(cmd->data, data);	
     	//return set_keyvalue(&cmd);
     	return get_keyvalue(cmd);
}

long kv_delete( __u64 key)
{
     	keyvalue_delete * cmd = (keyvalue_delete *) malloc(sizeof(keyvalue_delete));
     	cmd->key = key;     
	
	return delete_keyvalue(cmd);
}

void* remover()
{
	int key	= 3;
	if( kv_delete(key)  == 1)
	{
		fprintf(stdout, " deleted key= %d successfully", key);
	}
	else
	{
		fprintf(stdout, " delete failed");
	}
}

void* producer()
{	

	int number_of_keys = 10;	
	int a, i;
	int response;
	__u64 size;
	__u64 key;
	srand((int)time(NULL)+(int)getpid());
	char data[1024];	
	
	for(i = 0; i < number_of_keys; i++)
    	{
		memset(data, 0, 1024);
        	a = rand();
       		sprintf(data,"%d",a);
		response = kv_set(i, strlen(data), data);
        	fprintf(stdout,"Write:\t%d\t%d\t%zu\t%s\n",response,i,strlen(data),data);
   	}	
	

}

void* consumer()
{
	int number_of_keys = 10;	
	int a, i;
	int response;
	__u64 size;
	__u64 key;
	srand((int)time(NULL)+(int)getpid());
	char data[1024];	
	
	for(i = 0; i < number_of_keys; i++)
    	{
		memset(data, 0, 1024);
        //	a = rand();
       		sprintf(data,"%d",a);
			// only using key values for detection
		#ifdef DEBUG_MODE_1
			fprintf(stdout," searching key: %d", i);
		#endif
		response = kv_get(i, strlen(data), data);
		if(response == 1)
		{
			fprintf(stdout," Found element with key= %d\n\r", i);
		}
		else
		{
			fprintf(stdout,"\n Did not find element with key= %d", i);
		}
 
    	}	

}

int main()
{
	printf("Begin\n");
	pthread_t p[4];
	pthread_t c[4];
	pthread_t d[4];
	int i;
	
	//initialize lock;
	lock = (rwlock_t*)malloc(sizeof(rwlock_t));
	if(lock == NULL)
	{
		fprintf(stderr, "Could not allocate memory for lock");
		exit(0);
	}
	rwlock_init(lock);

	// allcoate memory to map
	initializeDictionary();	
	
	// write values insdide map
	for(i = 0; i<4; i++)
	{
		pthread_create(&p[i], NULL, producer, NULL);
	}
	
/*	for(i=0; i<2; i++)
	{		
		pthread_join(p[i], NULL);
	}*/	

	// delete a few values
	//pthread_create(&p, NULL, remover, 3);
	//pthread_create(&p, NULL, remover, NULL);
	//pthread_join(p, NULL);

/*	for(i = 0; i<2; i++)
	{
		pthread_create(&d[i], NULL, remover, NULL);
	}
	
	for(i=0; i<2; i++)
	{		
		pthread_join(d[i], NULL);
	}*/	


	printf("\n");
	
	// read values in the map
	// producer hard codes them to 20
	//pthread_create(&p, NULL, consumer, 20);
	//pthread_create(&p, NULL, consumer, NULL);

	for(i = 0; i<4; i++)
	{
		pthread_create(&c[i], NULL, consumer, NULL);
	}
			
	for(i=0; i<4; i++)
	{		
		pthread_join(p[i], NULL);
	}
	
	for(i=0; i<4; i++)
	{		
		pthread_join(c[i], NULL);
	}	

	//pthread_join(p, NULL);

	printf("\nEnd\n");

	return 0;

}
