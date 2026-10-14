#include <stdio.h>
#include "loom/loom.h"
static void ev(const char*a,const char*b,void*u){(void)a;(void)b;(void)u;}
int main(int argc,char**argv){
  char opts[512]; snprintf(opts,sizeof opts,"{\"data_dir\":\"%s\"}",argv[1]);
  for(int i=0;i<2;i++){
    const char*err=0; LoomContext*c=loom_init_ex(opts,&err); if(!c){puts("init failed");return 2;}
    loom_subscribe(c,"*",ev,0);
    loom_shutdown(c); puts("shutdown ok");
  }
  return 0;}
