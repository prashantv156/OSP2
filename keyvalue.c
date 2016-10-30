//////////////////////////////////////////////////////////////////////
//                             North Carolina State University
//
//
//
//                             Copyright 2016
//
////////////////////////////////////////////////////////////////////////
//
// This program is free software; you can redistribute it and/or modify it
// under the terms and conditions of the GNU General Public License,
// version 2, as published by the Free Software Foundation.
//
// This program is distributed in the hope it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
//
////////////////////////////////////////////////////////////////////////
//
//   Author:  Hung-Wei Tseng
//
//   Description:
//     Skeleton of KeyValue Pseudo Device
//
////////////////////////////////////////////////////////////////////////

#include "keyvalue.h"

#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/poll.h>

// added by ishan
#define DEBUG_MODE_1
#define MAX_NUMBER_OF_KEY_VALUE_PAIRS 256

//////////////////////////// READERS WRITERS LOCK ///////////////////////////////////

typedef struct _read_write_Lock_t
{
	struct semaphore 	lock; // binary semaphore (basic lock)
	struct semaphore 	writelock; // used to allow ONE writer or MANY readers
	int 			readers; // count of readers reading in critical section
} read_write_Lock_t;

void read_write_Lock_init(read_write_Lock_t *rw)
{
	rw->readers 	= 0;
	sema_init(&rw->lock, 1);
	sema_init(&rw->writelock, 1);
	// DECLARE_MUTEX(name_of_semaphore)
}


void read_write_Lock_acquire_readlock(read_write_Lock_t *rw) 
{
	if(down_interruptible(&rw->lock) != 0)
	{
		printk(KERN_INFO "could not hold semaphore used for rw->lock");
		return;
	}
	rw->readers++;
	if (rw->readers == 1)
	{
		if(down_interruptible(&rw->writelock) != 0) // first reader acquires writelock
		{
			printk(KERN_INFO "could not hold semaphore used for rw->writelock");
			return;
		}
	}
	up(&rw->lock);
}

void read_write_Lock_release_readlock(read_write_Lock_t *rw) 
{
	if(down_interruptible(&rw->lock) != 0)
	{
		printk(KERN_INFO "could not hold semaphore used for rw->lock");
		return;
	}
	rw->readers--;
	if (rw->readers == 0)
	{
		up(&rw->writelock); // last reader releases writelock
	}
	up(&rw->lock);
}


void read_write_Lock_acquire_writelock(read_write_Lock_t *rw)
{
	if(down_interruptible(&rw->writelock) != 0)
	{
		printk(KERN_INFO "could not hold semaphore used for rw->writelock");
		return;
	}
}
 
void read_write_Lock_release_writelock(read_write_Lock_t *rw)
{	
	up(&rw->writelock);
}

read_write_Lock_t* lock = NULL;

/////////////////////////////////////////////////////////////////////////////////////

static __u64 hash(__u64 key)
{
	__u64 temp;
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


////////////////////////////////////////////////////////////////////////////////////


unsigned transaction_id;
static void free_callback(void *data)
{
}

static long keyvalue_get(struct keyvalue_get __user *ukv)
{
    //struct keyvalue_get kv;

	__u64 key_to_be_fetched;
	struct dictnode* curr;
	
	key_to_be_fetched	= hash(ukv->key);
	curr			= map->hashmap[key_to_be_fetched];
	
	read_write_Lock_acquire_readlock(lock);
		
	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "Requested key %llu is yet to be inserted in the map\n", ukv->key);
		#endif
		read_write_Lock_release_readlock(lock);
		//return -1;
		return transaction_id++;
	}
	else
	{
		while(curr != NULL)
		{
			if(curr->kventry->key	== ukv->key)
			{
				#ifdef DEBUG_MODE_1
        				printk(KERN_INFO "Read:\tkey: %llu\tsize: %llu\tdata: %s\n",curr->kventry->key, curr->size, (char *)curr->kventry->data);
				#endif
				read_write_Lock_release_readlock(lock);
				//return 1;
				return transaction_id;
			}
			curr	= curr->next;
		}
		
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "Requested key %llu is yet to be inserted in the map\n", ukv->key);
		#endif
		read_write_Lock_release_readlock(lock);
		//return -1;
		return transaction_id++;
	}

    return transaction_id++;
}

static long keyvalue_set(struct keyvalue_set __user *ukv)
{
    //struct keyvalue_set kv;

	__u64 key_to_be_set;
	struct dictnode* val;
	struct dictnode* head ;

	head	= NULL;
	val	= NULL;

	key_to_be_set		= hash(ukv->key);
	val		 	= map->hashmap[key_to_be_set];
	
	read_write_Lock_acquire_writelock(lock);

	if(val == NULL)
	{
	
		head		= (struct dictnode*)kmalloc(sizeof(struct dictnode), GFP_KERNEL);
		head->kventry 	= (struct keyvalue_base* ) kmalloc(sizeof(struct keyvalue_base), GFP_KERNEL);
		if(head == NULL)
		{
			printk(KERN_ERR "Memory allocation to node in dictionary failed!\n");
			read_write_Lock_release_writelock(lock);
			//exit(0);
		}
		head->size = ukv->size;
		head->kventry->data	= (void *) kmalloc((head->size ) * (sizeof(void)), GFP_KERNEL);
		head->kventry->size 	= ukv->size;
		head->kventry->key	= ukv->key;
		memcpy(head->kventry->data, ukv->data, ukv->size);
		head->next = NULL;			

		map->hashmap[key_to_be_set] = head;
		read_write_Lock_release_writelock(lock);
		//return 1;  
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "KEYVALUE device: Write: First Node added at map[%llu]: \n",key_to_be_set);
		#endif
		return transaction_id++;

	}
	else
	{	
		if(val->next == NULL)
		{	
			if(val->kventry->key == ukv->key)
			{	
				strcpy(val->kventry->data, (ukv)->data);
			}
			else
			{	
				head 		= (struct dictnode*)kmalloc(sizeof(struct dictnode), GFP_KERNEL);
				head->kventry 	= (struct keyvalue_base* ) kmalloc(sizeof(struct keyvalue_base), GFP_KERNEL);
				if(head == NULL)
				{
					printk(KERN_ERR "Memory allocation to node in dictionary failed!\n");
					read_write_Lock_release_writelock(lock);
					//exit(0);
				}
				head->size = ukv->size;
				head->kventry->data	= (void *) kmalloc((head->size ) * (sizeof(void)), GFP_KERNEL);
				head->kventry->size 	= ukv->size;
				head->kventry->key	= ukv->key;
				memcpy(head->kventry->data, ukv->data, ukv->size);
				
				head->next = NULL;
				val->next = head;
				#ifdef DEBUG_MODE_1
					printk(KERN_INFO "KEYVALUE device: Write: New Node inserted at map[%llu]: \n",key_to_be_set);
				#endif
			}
		}
		else
		{
			while(val->next != NULL)
			{	
				if(val->kventry->key == ukv->key)
				{
					strcpy(val->kventry->data,ukv->data);
					read_write_Lock_release_writelock(lock);
					//return 1;
					#ifdef DEBUG_MODE_1
						printk(KERN_INFO "KEYVALUE device: Write: Node overwritten at map[%llu]: \n",key_to_be_set);
					#endif
					return transaction_id++;
				}								
				val = val->next;	
			}

			head		= (struct dictnode*)kmalloc(sizeof(struct dictnode), GFP_KERNEL);
			head->kventry 	= (struct keyvalue_base* ) kmalloc(sizeof(struct keyvalue_base), GFP_KERNEL);
			if(head == NULL)
			{
				printk(KERN_ERR "Memory allocation to node in dictionary failed!\n");
				read_write_Lock_release_writelock(lock);
				//exit(0);
			}
			head->size = ukv->size;
			head->kventry->data	= (void *) kmalloc((head->size ) * (sizeof(void)), GFP_KERNEL);
			head->kventry->size 	= ukv->size;
			head->kventry->key	= ukv->key;
			memcpy(head->kventry->data, ukv->data, ukv->size);
			
			head->next = NULL;
		
			val->next = head;
		}
		read_write_Lock_release_writelock(lock);
		//return 1;
		return transaction_id++;
	}

	//return 0;
    return transaction_id++;
}

static long keyvalue_delete(struct keyvalue_delete __user *ukv)
{
    //struct keyvalue_delete kv;

	__u64 key_to_be_deleted;
	struct dictnode* curr ;	

	key_to_be_deleted	= hash(ukv->key);
	curr			= map->hashmap[key_to_be_deleted];

	read_write_Lock_acquire_writelock(lock);

	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "Map is empty. Cannot perform delete operation!\n");
		#endif
		read_write_Lock_release_writelock(lock);
		//return -1;
    		return transaction_id++;
	}
	else if(curr->kventry->key	== ukv->key)
	{
		map->hashmap[key_to_be_deleted]	= curr->next;
		kfree(curr->kventry->data);
		kfree(curr->kventry);
		kfree(curr);
		read_write_Lock_release_writelock(lock);
		//return 1;
    		return transaction_id++;
	}
	else
	{
		struct dictnode* prev	= curr;
		while(curr != NULL)
		{
			if(curr->kventry->key	== ukv->key)
			{
				prev->next	= curr->next;
				kfree(curr->kventry->data);
				kfree(curr->kventry);
				kfree(curr);
				read_write_Lock_release_writelock(lock);
				//return 1;
    				return transaction_id++;
			}
			prev	= curr;
			curr 	= curr->next;
		}

		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "The requested key is non-existant in the map!\n");
		#endif
		read_write_Lock_release_writelock(lock);
		//return -1;
    		return transaction_id++;
	}


    return transaction_id++;
}

//Added by Hung-Wei
     
unsigned int keyvalue_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned int mask = 0;
    printk("keyvalue_poll called. Process queued\n");
    return mask;
}

static long keyvalue_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg)
{
    switch (cmd) {
    case KEYVALUE_IOCTL_GET:
        return keyvalue_get((void __user *) arg);
    case KEYVALUE_IOCTL_SET:
        return keyvalue_set((void __user *) arg);
    case KEYVALUE_IOCTL_DELETE:
        return keyvalue_delete((void __user *) arg);
    default:
        return -ENOTTY;
    }
}

static int keyvalue_mmap(struct file *filp, struct vm_area_struct *vma)
{
    return 0;
}

static const struct file_operations keyvalue_fops = {
    .owner                = THIS_MODULE,
    .unlocked_ioctl       = keyvalue_ioctl,
    .mmap                 = keyvalue_mmap,
//    .poll		  = keyvalue_poll,
};

static struct miscdevice keyvalue_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "keyvalue",
    .fops = &keyvalue_fops,
};

static int __init keyvalue_init(void)
{
    int ret;
	
	// added by ishan
	__u64 i;

	read_write_Lock_init(lock);
	#ifdef DEBUG_MODE_1
		printk(KERN_INFO "KEYVALUE device: just initialized semaphores!\n");
	#endif

	map = (struct dictionary*)kmalloc(sizeof(struct dictionary), GFP_KERNEL);	
	if(map == NULL)
	{
		printk(KERN_ERR "Memory allocation to map failed!\n");
		//exit(0);
	}
	#ifdef DEBUG_MODE_1
		printk(KERN_INFO "KEYVALUE device: allocated memory to map!\n");
	#endif
	for(i=0; i< MAX_NUMBER_OF_KEY_VALUE_PAIRS; i++)
	{
		map->hashmap[i]	= NULL;
	}
	// initialization of map ends here


    if ((ret = misc_register(&keyvalue_dev)))
        printk(KERN_ERR "Unable to register \"keyvalue\" misc device\n");
    return ret;
}

static void __exit keyvalue_exit(void)
{
	// added by Ishan
	__u64 i;
	struct dictnode * curr_node;
	struct dictnode * temp_node;
	for(i =0; i<MAX_NUMBER_OF_KEY_VALUE_PAIRS; i++)
	{
		curr_node	= map->hashmap[i];
		while(curr_node != NULL)
		{
			temp_node	= curr_node;
			curr_node	= curr_node->next;
			kfree(temp_node->kventry->data);
			kfree(temp_node->kventry);
			kfree(temp_node);
		}
	}
	kfree(map);
	#ifdef DEBUG_MODE_1
		printk(KERN_INFO "KEYVALUE device: deallocated all memory!");
	#endif
    misc_deregister(&keyvalue_dev);
}

MODULE_AUTHOR("Hung-Wei Tseng <htseng3@ncsu.edu>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
module_init(keyvalue_init);
module_exit(keyvalue_exit);
