#include <eza/arch/types.h>
#include <eza/kcontrol.h>
#include <eza/errno.h>
#include <eza/usercopy.h>
#include <mlibc/stddef.h>

extern long initrd_start_page,initrd_num_pages;

static kcontrol_node_t __kernel_boot_subdirs[] = {
  {
    .id=KCTRL_INITRD_START_PAGE,
    .type=KCTRL_DATA_LONG,
    .data=&initrd_start_page,
    .data_size=sizeof(long),
  },
  {
    .id=KCTRL_INITRD_SIZE,
    .type=KCTRL_DATA_LONG,
    .data=&initrd_num_pages,
    .data_size=sizeof(long),
  },
};

static kcontrol_node_t __kernel_subdirs[] = {
  {
    .id=KCTRL_BOOT_INFO,
    .num_subdirs=ARRAY_SIZE(__kernel_boot_subdirs),
    .subdirs=__kernel_boot_subdirs,
  },
};

#define NUM_ROOT_NODES  1
static kcontrol_node_t __root_knodes[NUM_ROOT_NODES] = {
  {
    .id=KCTRL_KERNEL,
    .num_subdirs=ARRAY_SIZE(__kernel_subdirs),
    .subdirs=__kernel_subdirs,
  },
};

static long __process_node(kcontrol_node_t *target,kcontrol_args_t *arg)
{
  int copysize,newsize;

  if( arg->old_data ) {
    if( copy_to_user(arg->old_data,target->data,target->data_size) ) {
      return -EFAULT;
    }
  }

  if( arg->old_data_size ) {
    if( copy_to_user(arg->old_data_size,&target->data_size,sizeof(target->data_size)) ) {
      return -EFAULT;
    }
  }

  if( !(arg->new_data_size | (long)arg->new_data) ) {
    return 0;
  }

  return -EINVAL;
  
  if( target->max_data_size && arg->new_data_size > target->max_data_size ) {
    return -EINVAL;
  }

  if( target->logic ) {
    return target->logic(target,arg);
  }

  switch( target->type ) {
    case KCTRL_DATA_LONG: /* By default accept only 1 long. */
      if( arg->new_data_size != 1 ) {
        return -EINVAL;
      }
      copysize=sizeof(long);
      newsize=1;
      break;
    case KCTRL_DATA_CHAR:
      newsize=copysize=arg->new_data_size;
      break;
    default:
      return -EINVAL;
  }

  if( copy_from_user(target->data,arg->new_data,copysize) ) {
    return -EFAULT;
  }

  target->data_size=newsize;
  return 0;
}

long sys_kernel_control(kcontrol_args_t *arg)
{
  kcontrol_args_t kargs;
  kcontrol_node_t *nodes;
  int id,*_pid,len,size;
  kcontrol_node_t *target=NULL;

  if( copy_from_user(&kargs,arg,sizeof(kargs)) ) {
    return -EFAULT;
  }

  _pid=kargs.name;
  len=kargs.name_len;

  if( !len || len > KCTRL_MAX_NAME_LEN ) {
    return -EINVAL;
  }

  nodes=__root_knodes;
  size=NUM_ROOT_NODES;

process_nodes:
  if( len ) {
    if( copy_from_user(&id,_pid,sizeof(id)) ) {
      return -EFAULT;
    }

    while( size-- ) {
      if( nodes->id == id ) {
        if( nodes->subdirs ) {
          size=nodes->num_subdirs;
          nodes=nodes->subdirs;
          len--;
          _pid++;
          goto process_nodes;
        } else if( len == 1 ) {
          target=nodes;
          break;
        }
      }
      nodes++;
    }
  }

  if( target ) {
    return __process_node(target,&kargs);
  } else {
    return -EINVAL;
  }
}
