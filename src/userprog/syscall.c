#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include<float.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include"threads/malloc.h"
#include"devices/input.h"

static void syscall_handler(struct intr_frame*);
bool check_string(const char*);
bool check_ptr(uint32_t*);
struct thread_file*find_file(int);
void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }
void sys_exit(int);
static void syscall_handler(struct intr_frame* f UNUSED) {
  if(!check_ptr((uint32_t*)f->esp))
  {
    sys_exit(-1);
    return;
  }

  uint32_t*args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

   printf("System call number: %d\n", args[0]);

  if (args[0] == SYS_EXIT) {
    if(!check_ptr(&args[1]))
    f->eax=-1;
    else
    f->eax = args[1];
    sys_exit(f->eax);
  }

  if(args[0]==SYS_PRACTICE)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }
    f->eax=args[1]+1;
  }

  if(args[0]==SYS_HALT)
  {
    shutdown_power_off();
  }

  if(args[0]==SYS_EXEC)
  {
    if(!check_ptr(&args[1])||!check_string(args[1]))
    {
      sys_exit(-1);
      return;
    }
    f->eax=process_execute(args[1]);
  }

  if(args[0]==SYS_WAIT)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }
    int ch_pid=args[1];
    f->eax=process_wait(ch_pid);
  }

  /*以下是文件系统调用*/

  if(args[0]==SYS_CREATE)
  {
    if(!check_ptr(&args[1])||!check_ptr(&args[2])||!check_string(args[1]))
    {
      sys_exit(-1);
      return;
    }
    char*file=args[1];
    int initial_size=args[2];
    f->eax=filesys_create(file,initial_size);
  }

  if(args[0]==SYS_REMOVE)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }
    char*file=args[1];
    if(!check_string(file))
    {
      f->eax= false;
      return;
    }
    bool success=filesys_remove(file);
    f->eax=success;
  }

  if(args[0]==SYS_OPEN)
  {
    if(!check_ptr(&args[1])||!check_string(args[1]))
    {
      sys_exit(-1);
      return;
    }
    char*file=args[1];
    struct thread*cur=thread_current();

    struct thread_file* tmp=malloc(sizeof(struct thread_file));
    tmp->fd=cur->cur_file_fd++;
    tmp->f=filesys_open(file);
    if(tmp->f==NULL)
    {
      f->eax=-1;
      return;
    }
    list_push_back(&cur->open_files,&tmp->elem_tf);
    f->eax=tmp->fd;
  }

  if(args[0]==SYS_CLOSE)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }
    int fd=args[1];
    struct thread_file*tf=find_file(fd);
    if(!tf)
    {
      return;
    }
    file_close(tf->f);
    list_remove(&tf->elem_tf);
    free(tf);
  }

  if(args[0]==SYS_FILESIZE)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }
    int fd=args[1];
    struct thread_file*tf=find_file(fd);
    if(!tf)
    {
      f->eax=-1;
      return;
    }
    f->eax=file_length(tf->f);
  }

  if(args[0]==SYS_READ)
  {
    if(!check_ptr(&args[1])||!check_ptr(&args[2])||!check_ptr(&args[3]))
    {
      sys_exit(-1);
      return;
    }
    int fd=args[1];
    char*buffer=args[2];
    int size=args[3];

    if(!check_string(buffer))
    {
      sys_exit(-1);
      return;
    }

    if(fd==STDIN_FILENO)
    {
      int total=0;
      for(int i=0;i<size;i++)
      {
        buffer[i]=input_getc();
        total++;
      }
      f->eax=total;
      return;
    }
    struct thread_file*tf=find_file(fd);
    if(!tf)
    {
      f->eax=-1;
      return;
    }
    f->eax=file_read(tf->f,buffer,size);
  }

  if(args[0]==SYS_WRITE)
  {
    if(!check_ptr(&args[1])||!check_ptr(&args[2])||!check_ptr(&args[3]))
    {
      sys_exit(-1);
      return;
    }
    int fd=args[1];
    char*buffer=args[2];
    int size=args[3];

    if(!check_string(buffer))
    {
      sys_exit(-1);
      return;
    }

    if(fd==STDOUT_FILENO)
    {
      putbuf(buffer,size);
      f->eax=size;
      return;
    }
    struct thread_file*tf=find_file(fd);
    if(!tf)
    {
      f->eax=-1;
      return;
    }
    f->eax=file_write(tf->f,buffer,size);
  }

  if(args[0]==SYS_TELL)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }
    int fd=args[1];
    struct thread_file*tf=find_file(fd);
    if(tf!=NULL)
    {
      f->eax=file_tell(tf->f);
    }
  }

  if(args[0]==SYS_SEEK)
  {
    if(!check_ptr(&args[1])||!check_ptr(&args[2]))
    {
      sys_exit(-1);
      return;
    }
    int fd=args[1];
    unsigned pos=args[2];
    struct thread_file*tf=find_file(fd);
    if(tf!=NULL)
    {
      file_seek(tf->f,pos);
    }
  }

  if(args[0]==SYS_COMPUTE_E)
  {
    if(!check_ptr(&args[1]))
    {
      sys_exit(-1);
      return;
    }

    f->eax=sys_sum_to_e(args[1]);
  }
}

bool check_string(const char*being_checked)
{
  if(being_checked==NULL||being_checked>PHYS_BASE)return false;
  int i=0;
  if(!is_user_vaddr(&being_checked[i])||!pagedir_get_page(thread_current()->pcb->pagedir,&being_checked[i]))
  return false;
  for(;being_checked[i]!='\0';i++)
  {
    if(!is_user_vaddr(&being_checked[i+1])||!pagedir_get_page(thread_current()->pcb->pagedir,&being_checked[i+1]))
    return false;
  }
  return true;
}
bool check_ptr(uint32_t* unchecked)
{
  for(int i=0;i<4;i++)
  {
    if(!is_user_vaddr(unchecked+i)||!pagedir_get_page(thread_current()->pcb->pagedir,unchecked+i))
    return false;
  }
  return true;
}
struct thread_file*find_file(int fd)
{
  struct thread*cur=thread_current();
  struct list_elem*e;
  for(e=list_begin(&cur->open_files);e!=list_end(&cur->open_files);e=list_next(e))
  {
    struct thread_file*tmp=list_entry(e,struct thread_file,elem_tf);
    if(tmp->fd==fd)
    return tmp;
  }
  return NULL;
}

void sys_exit(int exit_status)
{
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, exit_status);
  thread_current()->exit_status=exit_status;
  process_exit();
}