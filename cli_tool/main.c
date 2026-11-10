/* Green2 PASN command-line adapter; protocol and cache logic live in src/pasn/ and shared/. */
#include "green2_pasn.h"
#include "comm_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <net/if.h>
static int hex_line(const char *line, uint8_t *out, size_t len)
{
	const char *p = strstr(line, "hexdump(len=");
	size_t n = 0;
	unsigned int byte;

	if (!p || !(p = strstr(p, "): ")))
		return -1;
	p += 3;
	while (*p && n < len) {
		while (*p == ' ' || *p == '\t') p++;
		if (sscanf(p, "%2x", &byte) != 1) break;
		out[n++] = byte;
		if (!p[0] || !p[1]) break;
		p += 2;
		if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0')
			return -1;
	}
	return n == len ? 0 : -1;
}


int main(int argc,char **argv)
{
    if(argc>1&&!strcmp(argv[1],"events"))return green2_vendor_events_main(argc-1,argv+1);
    if(argc>1&&!strcmp(argv[1],"pasn")){argc--;argv++;}
    char *end,*line=NULL;size_t capacity=0;int pending=0;
    if(argc!=3){fprintf(stderr,"usage: %s IFACE FREQ_MHZ < supplicant.log.stream\n",argv[0]);return 2;}
    unsigned long frequency=strtoul(argv[2],&end,10);
    if(*end||frequency<2400||frequency>7125||!if_nametoindex(argv[1]))return 2;
    struct green2_pasn_config config={.interface_name=argv[1],.frequency=frequency,
        .nik_cache_path="/var/lib/nan/nik_cache",.npk_path=getenv("GREEN2_NPK_FILE"),
        .result_path=getenv("GREEN2_PASN_RESULT_FILE")};
    struct green2_pasn *engine=green2_pasn_create_config(&config);
    if(!engine){fprintf(stderr,"PASN initialization failed\n");return 1;}
    while(getline(&line,&capacity,stdin)>=0){
        if(strstr(line,"Vendor event: wiphy=")){
            pending=0;
            if(strstr(line,"vendor_id=0xe04c subcmd=29"))pending=29;
            else if(strstr(line,"vendor_id=0xe04c subcmd=30"))pending=30;
            continue;
        }
        uint8_t event[645]={0};
        if(pending==29&&strstr(line,"hexdump(len=645):")){
            pending=0;if(!hex_line(line,event,645))green2_pasn_start(engine,event,645,NULL,0);
        }else if(pending==30&&strstr(line,"hexdump(len=598):")){
            pending=0;if(!hex_line(line,event,598))green2_pasn_receive(engine,event,598);
        }
        volatile uint8_t *clear=event;for(size_t i=0;i<sizeof(event);i++)clear[i]=0;
    }
    green2_pasn_destroy(engine);
    if(line){volatile char *clear=line;for(size_t i=0;i<capacity;i++)clear[i]=0;free(line);}
    return 0;
}
