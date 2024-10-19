#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
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
struct thread_file*find_file(int);
void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit();
  }

  if(args[0]==SYS_PRACTICE)
  {
    f->eax=args[1]+1;
  }

  if(args[0]==SYS_HALT)
  {
    shutdown_power_off();
  }

  if(args[0]==SYS_EXEC)
  {
    char*cmd=args[1];
    if(!check_string(cmd))
    f->eax=-1;
    else
    f->eax=process_execute(cmd);
  }

  if(args[0]==SYS_WAIT)
  {
    int ch_pid=args[1];
    process_wait(ch_pid);
  }

  if(args[0]==SYS_CREATE)
  {
    char*file=args[1];
    int initial_size=args[2];
    if(!check_string(file))
    {
      f->eax= false;
      return;
    }
    bool success=filesys_create(file,initial_size);
    f->eax=success;
  }

  if(args[0]==SYS_REMOVE)
  {
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
    char*file=args[1];
    struct thread*cur=thread_current();

    if(!check_string(file))
    {
      f->eax= -1;
      return;
    }

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
    int fd=args[1];
    struct thread_file*tf=find_file(fd);
    if(!tf)
    {
      return;
    }
    file_close(tf->f);
    free(tf);
  }

  if(args[0]==SYS_FILESIZE)
  {
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
    int fd=args[1];
    char*buffer=args[2];
    int size=args[3];

    if(!check_string(buffer))
    {
      f->eax= -1;
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
    int fd=args[1];
    char*buffer=args[2];
    int size=args[3];

    if(!check_string(buffer))
    {
      f->eax= -1;
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
}

bool check_string(const char*being_checked)
{
  if(being_checked==NULL)return false;
  char*ptr;
  for(ptr=being_checked;*ptr!='\0';ptr++)
  {
    if(!is_user_vaddr(ptr)||!pagedir_get_page(thread_current()->pcb->pagedir,ptr))
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