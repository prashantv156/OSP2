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

////////////////////////////////////////////////////////////////////////
//
//	Project 2: Key Value Pseudo Device 
//	Team Members: Ishan Munje (idmunje@ncsu.edu)
//				  Prashant Vichare (pvichar@ncsu.edu)
//
////////////////////////////////////////////////////////////////////////

:
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
#include <linux/spinlock.h>
//#define DEBUG_MODE_1
#define MAX_NUMBER_OF_KEY_VALUE_PAIRS 256
//#define IMPLEMENTED_RWLOCK
#define LINUX_RWLOCK

//////////////////////////// READERS WRITERS LOCK ///////////////////////////////////

#ifdef IMPLEMENTED_RWLOCK
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

#endif

#ifdef LINUX_RWLOCK
//rwlock_t mr_rwlock	= RW_LOCK_UNLOCKED; // static
rwlock_t mr_rwlock;	// dynamic
#endif

/////////////////////////////////////////////////////////////////////////////////////

static __u64 hash(__u64 key)
{
	return (key%MAX_NUMBER_OF_KEY_VALUE_PAIRS);
}

struct keyvalue_base
{
	__u64 key;
	__u64 size;
	//char data[1024];
	void *data;
};

struct dictnode 
{
	struct dictnode* next;
	struct keyvalue_base* kventry;
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
	struct keyvalue_get kv;
	__u64 key_to_be_fetched;
	__u64 bytes_not_copied;

	struct dictnode* curr;
	
	kv.key	= ukv->key;
	kv.size	= ukv->size;
	kv.data	= ukv->data;
	key_to_be_fetched	= hash(kv.key);
	curr			= map->hashmap[key_to_be_fetched];

	#ifdef IMPLEMENTED_RWLOCK
	read_write_Lock_acquire_readlock(lock);
	#endif
	#ifdef LINUX_RWLOCK
		read_lock(&mr_rwlock);
	#endif
	
	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "Requested key %llu is yet to be inserted in the map\n", kv.key);
		#endif
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_readlock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			read_unlock(&mr_rwlock);
		#endif
		return -1;
	}
	else
	{
		while(curr != NULL)
		{
			if(curr->kventry->key	== kv.key)
			{
				#ifdef DEBUG_MODE_1
        				printk(KERN_INFO "Read:\tkey: %llu\tsize: %llu\tdata: %s data_addr: %u\n",curr->kventry->key, curr->kventry->size, (char *)curr->kventry->data, &curr->kventry->data);
				#endif
				
				bytes_not_copied = copy_to_user(kv.data, curr->kventry->data, curr->kventry->size);
				
			/*	if(bytes_not_copied != 0)
				{			
					#ifdef LINUX_RWLOCK
						write_unlock(&mr_rwlock);
					#endif
					return -1;
				}*/
			
				*(kv.size)	= curr->kventry->size;
				
				// WARNING: The user might not have allocated memory to kv.data
				// Possible bug ishan

				#ifdef IMPLEMENTED_RWLOCK
				read_write_Lock_release_readlock(lock);
				#endif
				#ifdef LINUX_RWLOCK
					read_unlock(&mr_rwlock);
				#endif
				//return 1;
				return transaction_id++;
			}
			curr	= curr->next;
		}
		
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "Requested key %llu is yet to be inserted in the map\n", kv.key);
		#endif
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_readlock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			read_unlock(&mr_rwlock);
		#endif
		return -1;
	}

    //return transaction_id++;
}

static 
struct dictnode* create_node(struct keyvalue_set* kv)
{
	// create a node
	__u64 bytes_not_copied;
	struct dictnode* head ;
	head	= (struct dictnode*) kmalloc(sizeof(struct dictnode), GFP_KERNEL);
	if(head == NULL)
	{
		printk(KERN_ERR "Memory allocation to node in dictionary failed!\n");
		return NULL;
	}
	head->next	= NULL;
	head->kventry	= (struct keyvalue_base*) kmalloc(sizeof(struct keyvalue_base), GFP_KERNEL);
	if(head->kventry == NULL)
	{
		printk(KERN_ERR "Memory allocation to kventry in dictionary failed!\n");
		return NULL;
	}
	head->kventry->size	= kv->size;
	head->kventry->key	= kv->key;
	head->kventry->data	= (void *) kmalloc((head->kventry->size ), GFP_KERNEL);
	if(head->kventry->data == NULL)
	{
		printk(KERN_ERR "Memory allocation to data in dictionary failed!\n");
		return NULL;
	}
	head->kventry->size	= kv->size;
	bytes_not_copied = copy_from_user(head->kventry->data, kv->data, kv->size);
	
	if(bytes_not_copied != 0)
	{
		printk(KERN_ERR "Could not copy all data from user space");
		kfree(head->kventry->data);
		kfree(head->kventry);
		kfree(head);			
		return NULL;

	}

	return head;
}

static long keyvalue_set(struct keyvalue_set __user *ukv)
{
    struct keyvalue_set kv;
	__u64 key_to_be_set;
	__u64 bytes_not_copied;
	struct dictnode* val;
	struct dictnode* prev;
	struct dictnode* head ;

	kv.key	= ukv->key;
	kv.size	= ukv->size;
	kv.data	= ukv->data;
	head	= NULL;
	val	= NULL;

	key_to_be_set		= hash(kv.key);
	val		 	= map->hashmap[key_to_be_set];
	#ifdef IMPLEMENTED_RWLOCK
	read_write_Lock_acquire_writelock(lock);
	#endif
	#ifdef LINUX_RWLOCK
		write_lock(&mr_rwlock);
	#endif

	if(kv.size > 4096 || kv.size < 1)
	{
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif
		return -1;
	}
	
	if(val == NULL)
	{
		head	= create_node(&kv);	
		if(head == NULL)
		{
		
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif
 		return -1;
		
		}

		map->hashmap[key_to_be_set] = head;
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "KEYVALUE device: Write: First Node added at map[%llu], size: %llu, key: %llu, data: %s: \n",key_to_be_set, head->kventry->size, head->kventry->key,(char*) head->kventry->data);
		#endif
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif

		return transaction_id++;

	}
	else
	{	
		if(val->next == NULL)
		{	
			if(val->kventry->key == kv.key)
			{		
				void* temp_data	= val->kventry->data;
				val->kventry->data = (void*) kmalloc(kv.size, GFP_KERNEL);
				bytes_not_copied = copy_from_user(val->kventry->data, kv.data, kv.size);
				if(bytes_not_copied != 0)
				{
					printk(KERN_ERR "Could not copy all bytes from user");
					kfree(val->kventry->data);			
					#ifdef LINUX_RWLOCK
						write_unlock(&mr_rwlock);
					#endif
					return -1;
				}

				kfree(temp_data);
				val->kventry->size	= kv.size;

				#ifdef DEBUG_MODE_1
					printk(KERN_INFO "KEYVALUE device: Write: First Node Overwritten at map[%llu], size: %llu, key: %llu, data: %s: \n",key_to_be_set, val->kventry->size, val->kventry->key,(char*) val->kventry->data);
				#endif
				#ifdef IMPLEMENTED_RWLOCK
				read_write_Lock_release_writelock(lock);
				#endif
				#ifdef LINUX_RWLOCK
					write_unlock(&mr_rwlock);
				#endif
				return transaction_id++;
			}
			else
			{	
				head	= create_node(&kv);	
				
				if(head == NULL)
				{
				
				#ifdef IMPLEMENTED_RWLOCK
				read_write_Lock_release_writelock(lock);
				#endif
				#ifdef LINUX_RWLOCK
					write_unlock(&mr_rwlock);
				#endif
 				return -1;
				
				}

				val->next = head;

				#ifdef DEBUG_MODE_1
					printk(KERN_INFO "KEYVALUE device: Write: Second Node inserted at tail at map[%llu], size: %llu, key: %llu, data: %s: \n",key_to_be_set, head->kventry->size, head->kventry->key, (char*)head->kventry->data);
				#endif
					#ifdef IMPLEMENTED_RWLOCK
					read_write_Lock_release_writelock(lock);
					#endif
					#ifdef LINUX_RWLOCK
						write_unlock(&mr_rwlock);
					#endif
				return transaction_id++;
			}
		}
		else
		{
			prev	= val;
			while(val != NULL)
			{	
				if(val->kventry->key == kv.key)
				{
					void* temp_data	= val->kventry->data;
					val->kventry->data = (void*) kmalloc(kv.size, GFP_KERNEL);
					bytes_not_copied = copy_from_user(val->kventry->data, kv.data, kv.size);
					
					if(bytes_not_copied != 0)
					{
						printk(KERN_ERR "Could not copy all data from user");
						kfree(val->kventry->data);			
						#ifdef LINUX_RWLOCK
							write_unlock(&mr_rwlock);
						#endif
						return -1;
					}

					kfree(temp_data);
					val->kventry->size	= kv.size;
					
					#ifdef DEBUG_MODE_1
						//printk(KERN_INFO "KEYVALUE device: Write: Node overwritten at map[%llu]: \n",key_to_be_set);
						printk(KERN_INFO "KEYVALUE device: Write: Node overwritten at map[%llu], size: %llu, key: %llu, data: %s, data_addr: %u: \n",key_to_be_set, val->kventry->size, val->kventry->key, (char *)val->kventry->data, &val->kventry->data);
					#endif
					#ifdef IMPLEMENTED_RWLOCK
					read_write_Lock_release_writelock(lock);
					#endif
					#ifdef LINUX_RWLOCK
						write_unlock(&mr_rwlock);
					#endif
					//return 1;
					return transaction_id++;
				}								
				prev	= val;
				val = val->next;	
			}

			//val->next = head;
			head	= create_node(&kv);
			if(head == NULL)
			{
			#ifdef IMPLEMENTED_RWLOCK
				read_write_Lock_release_writelock(lock);
			#endif
			#ifdef LINUX_RWLOCK
				write_unlock(&mr_rwlock);
			#endif
 			return -1;		
			}

			prev->next = head;
			#ifdef DEBUG_MODE_1
				printk(KERN_INFO "KEYVALUE device: Write: New Node inserted at tail at map[%llu], size: %llu, key: %llu, data: %s: \n",key_to_be_set, head->kventry->size, head->kventry->key, (char*)head->kventry->data);
			#endif
			#ifdef IMPLEMENTED_RWLOCK
			read_write_Lock_release_writelock(lock);
			#endif
			#ifdef LINUX_RWLOCK
				write_unlock(&mr_rwlock);
			#endif
			return transaction_id++;

		}

		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif
		//return 1;
		return transaction_id++;

	}

	#ifdef IMPLEMENTED_RWLOCK
	read_write_Lock_release_writelock(lock);
	#endif
	#ifdef LINUX_RWLOCK
		write_unlock(&mr_rwlock);
	#endif
	return -1;
    //return transaction_id++;
}

static long keyvalue_delete(struct keyvalue_delete __user *ukv)
{
    struct keyvalue_delete kv;
	__u64 key_to_be_deleted;
	struct dictnode* curr ;	

	kv.key	= ukv->key;
	key_to_be_deleted	= hash(kv.key);
	curr			= map->hashmap[key_to_be_deleted];

	#ifdef IMPLEMENTED_RWLOCK
	read_write_Lock_acquire_writelock(lock);
	#endif
	#ifdef LINUX_RWLOCK
		write_lock(&mr_rwlock);
	#endif

	if(curr == NULL)
	{
		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "Map is empty. Cannot perform delete operation!\n");
		#endif
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif
		return -1;
	}

	else if(curr->kventry->key	== kv.key)
	{
		map->hashmap[key_to_be_deleted]	= curr->next;
		kfree(curr->kventry->data);
		kfree(curr->kventry);
		kfree(curr);
		#ifdef IMPLEMENTED_RWLOCK
		read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif
    		return transaction_id++;
	}

	else
	{
		struct dictnode* prev	= curr;
		while(curr != NULL)
		{
			if(curr->kventry->key	== kv.key)
			{
				prev->next	= curr->next;
				kfree(curr->kventry->data);
				kfree(curr->kventry);
				kfree(curr);
				#ifdef IMPLEMENTED_RWLOCK
				read_write_Lock_release_writelock(lock);
				#endif
				#ifdef LINUX_RWLOCK
					write_unlock(&mr_rwlock);
				#endif
    				return transaction_id++;
			}
			prev	= curr;
			curr 	= curr->next;
		}

		#ifdef DEBUG_MODE_1
			printk(KERN_INFO "The requested key is non-existant in the map!\n");
		#endif
		#ifdef IMPLEMENTED_RWLOCK
			read_write_Lock_release_writelock(lock);
		#endif
		#ifdef LINUX_RWLOCK
			write_unlock(&mr_rwlock);
		#endif
		return -1;
	}

	#ifdef IMPLEMENTED_RWLOCK
	read_write_Lock_release_writelock(lock);
	#endif
	#ifdef LINUX_RWLOCK
		write_unlock(&mr_rwlock);
	#endif
	return -1;
   
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

	#ifdef LINUX_RWLOCK
		rwlock_init(&mr_rwlock);
	#endif
	#ifdef IMPLEMENTED_RWLOCK
	lock	= (read_write_Lock_t*) kmalloc(sizeof(read_write_Lock_t), GFP_KERNEL);
	if(lock == NULL)
	{
		printk(KERN_ERR "KEYVALUE device: Memory allocation to lock variable failed!");
		return -1;
	}
	read_write_Lock_init(lock);
	#endif
	#ifdef DEBUG_MODE_1
		printk(KERN_INFO "KEYVALUE device: just initialized semaphores!\n");
	#endif

	map = (struct dictionary*)kmalloc(sizeof(struct dictionary), GFP_KERNEL);	
	if(map == NULL)
	{
		printk(KERN_ERR "Memory allocation to map failed!\n");
		return -1;
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
