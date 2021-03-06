#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <asm/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define DEBUG_MODE_1
#define  MAX_NUMBER_OF_KEY_VALUE_PAIRS 256

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
	return (key ^ (temp>>10) ^ (temp>>20)) & MAX_NUMBER_OF_KEY_VALUE_PAIRS;
}

struct keyvalue_base
{
	__u64 key;
	__u64 size;
	//char data[1024];
	void *data;
	// replace with char* data
	// dont forget to allocate memory to data and copy ukv->kventry->data into it
};
typedef struct keyvalue_base str_keyvalue_set;
typedef struct keyvalue_base str_keyvalue_get;
typedef struct keyvalue_base str_keyvalue_delete;

struct dictnode 
{
	struct dictnode* next;
	struct keyvalue_base* kventry;
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
		fprintf(stderr,"Memory allocation to map failed!\n");
		exit(0);
	}
	int i = 0;
	for(i=0; i< MAX_NUMBER_OF_KEY_VALUE_PAIRS; i++)
	{
		map->hashmap[i]	= NULL;
	}
}

static long keyvalue_get(str_keyvalue_get* ukv)
{
	int key_to_be_fetched	= hash(ukv->key);
	struct dictnode* curr	= map->hashmap[key_to_be_fetched];
	
	rwlock_acquire_readlock(lock);
		
	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			fprintf(stdout,"Requested key %llu is yet to be inserted in the map\n", ukv->key);
		#endif
		rwlock_release_readlock(lock);
		return -1;
	}
	else
	{
		while(curr != NULL)
		{
			if(curr->kventry->key	== ukv->key)
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
			fprintf(stdout,"Requested key %llu is yet to be inserted in the map\n", ukv->key);
		#endif
		rwlock_release_readlock(lock);
		return -1;
	}

}

static long delete_keyvalue(str_keyvalue_delete* ukv)
{
	int key_to_be_deleted = hash(ukv->key);
	struct dictnode* curr = map->hashmap[key_to_be_deleted];

	rwlock_acquire_writelock(lock);

	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			fprintf(stdout, "Map is empty. Cannot perform delete operation!\n");
		#endif
		rwlock_release_writelock(lock);
		return -1;
	}
	else if(curr->kventry->key	== ukv->key)
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
			if(curr->kventry->key	== ukv->key)
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
			fprintf(stdout, "The requested key is non-existant in the map!\n");
		#endif
		rwlock_release_writelock(lock);
		return -1;
	}
}

static long keyvalue_set( str_keyvalue_set* ukv )
{
	
	int key_to_be_set = hash(ukv->key);
	struct dictnode* val = map->hashmap[key_to_be_set];
	
	rwlock_acquire_writelock(lock);

	if(val == NULL)
	{
	
		struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
		head->kventry = (struct keyvalue_base* ) malloc(sizeof(struct keyvalue_base));
		if(head == NULL)
		{
			fprintf(stderr,"Memory allocation to node in dictionary failed!\n");
			rwlock_release_writelock(lock);
			exit(0);
		}
		head->size = ukv->size;
		head->kventry->data	= (void *) malloc((head->size ) * (sizeof(void)));
		head->kventry = ukv;
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
				strcpy(val->kventry->data, (ukv)->data);
			}
			else
			{	
				struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
				head->kventry = (struct keyvalue_base* ) malloc(sizeof(struct keyvalue_base));
				if(head == NULL)
				{
					fprintf(stderr,"Memory allocation to node in dictionary failed!\n");
					rwlock_release_writelock(lock);
					exit(0);
				}
				head->size = ukv->size;
				head->kventry->data	= (void *) malloc((head->size ) * (sizeof(void)));
				head->kventry = ukv;
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
					strcpy(val->kventry->data,ukv->data);
					rwlock_release_writelock(lock);
					return 1;
				}								
				val = val->next;	
			}

			struct dictnode* head = (struct dictnode*)malloc(sizeof(struct dictnode));
			head->kventry = (struct keyvalue_base* ) malloc(sizeof(struct keyvalue_base));
			if(head == NULL)
			{
				fprintf(stderr,"Memory allocation to node in dictionary failed!\n");
				rwlock_release_writelock(lock);
				exit(0);
			}
			head->size = ukv->size;
			head->kventry->data	= (void *) malloc((head->size ) * (sizeof(void)));
			head->kventry = ukv;
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
     	str_keyvalue_set * cmd = (str_keyvalue_set *) malloc(sizeof(str_keyvalue_set));
     	cmd->key = key;     
     	cmd->size = size;
	cmd->data = (void *) malloc( (cmd->size) * (sizeof(void)) );
     	strcpy(cmd->data, data);	
     	//return keyvalue_set(&cmd);
     	return keyvalue_set(cmd);
}

long kv_get(__u64 key, __u64 size, void *data)
{
     	str_keyvalue_get * cmd = (str_keyvalue_get *) malloc(sizeof(str_keyvalue_get));
     	cmd->key = key;     
     	cmd->size = size;
	cmd->data = (void *) malloc( (cmd->size) * (sizeof(void)) );
     	strcpy(cmd->data, data);	
     	//return keyvalue_get(&cmd);
     	return keyvalue_get(cmd);
}

long kv_delete( __u64 key)
{
     	str_keyvalue_delete * cmd = (str_keyvalue_delete *) malloc(sizeof(str_keyvalue_delete));
     	cmd->key = key;     
	
	return delete_keyvalue(cmd);
}

void* remover()
{
	int key	= 3;
	if( kv_delete(key)  == 1)
	{
		fprintf(stdout, "deleted key= %d successfully\n", key);
	}
	else
	{
		fprintf(stdout, "delete failed\n");
	}
}

void* producer()
{	
	struct timeval tv;
	gettimeofday(&tv, NULL);
	suseconds_t curr_time = 0;
	curr_time = tv.tv_usec;

	int number_of_keys = 10;	
	int a, i;
	int response;
	__u64 size;
	__u64 key;
	srand((int)curr_time + (int)getpid());
	char data[1024];	
	
	for(i = 0; i < number_of_keys; i++)
    	{
		gettimeofday(&tv, NULL);
		curr_time = tv.tv_usec;
		memset(data, 0, 1024);
        	a = rand();
       		sprintf(data,"%d",a);
		response = kv_set(i, strlen(data), data);
        	fprintf(stdout,"Write:\t%d\t%d\t%zu\t%s\t%d\n",response,i,strlen(data),data,(int)curr_time);
   	}	
	

}

void* consumer()
{
	int number_of_keys = 10;		

	struct timeval tv;
	gettimeofday(&tv, NULL);
	suseconds_t time;

	int a, i;
	int response;
	__u64 size;
	__u64 key;

//	srand((int)time(NULL)+(int)getpid());

	char data[1024];	
	
	for(i = 0; i < number_of_keys; i++)
    	{
		memset(data, 0, 1024);
        //	a = rand();
       		sprintf(data,"%d",a);
			// only using key values for detection
		#ifdef DEBUG_MODE_1
			fprintf(stdout,"searching key: %d\n", i);
		#endif
	
		gettimeofday(&tv, NULL);
		time = tv.tv_usec;

		response = kv_get(i, strlen(data), data);
		if(response == 1)
		{
			fprintf(stdout,"Found element with key= %d\n", i);	
			fprintf(stdout,"Time at which key was found= %d\n", (int)time);
		}
		else
		{
			fprintf(stdout,"Did not find element with key= %d\n", i);	
			fprintf(stdout,"Time at which key was not found= %d\n", (int)time);
		}
 
    	}	

}

int main()
{
	pthread_t p[8];
	pthread_t c[4];
	pthread_t d[2];
	int i;
	
	//initialize lock;
	lock = (rwlock_t*)malloc(sizeof(rwlock_t));
	if(lock == NULL)
	{
		fprintf(stderr, "Could not allocate memory for lock\n");
		exit(0);
	}
	rwlock_init(lock);

	// allcoate memory to map
	initializeDictionary();	
	printf("Begin\n");
	
	// write values insdide map
	for(i = 0; i<8; i++)
	{
		pthread_create(&p[i], NULL, producer, NULL);
	}
	printf("Begin\n");
	
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
	

	for(i = 0; i<2; i++)
	{
		pthread_create(&d[i], NULL, remover, NULL);
	}

	printf("\n");

	// read values in the map
	// producer hard codes them to 20
	//pthread_create(&p, NULL, consumer, 20);
	//pthread_create(&p, NULL, consumer, NULL);

	for(i = 0; i<4; i++)
	{
		pthread_create(&c[i], NULL, consumer, NULL);
	}
			
	for(i=0; i<8; i++)
	{		
		pthread_join(p[i], NULL);
	}
	
	for(i = 0; i<2; i++)
	{
		pthread_join(d[i], NULL);
	}
	
	for(i=0; i<4; i++)
	{		
		pthread_join(c[i], NULL);
	}	

	//pthread_join(p, NULL);

	printf("\nEnd\n");

	return 0;

}
