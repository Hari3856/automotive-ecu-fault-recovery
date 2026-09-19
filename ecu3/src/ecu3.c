#include <stdio.h>
#include <stdlib.h>
#include <sys/dispatch.h>
#include <unistd.h>
#include <sys/neutrino.h>
#define HEARTBEAT 1
#define ECUID 3
int main(void) {
	int coid;
	coid=name_open("supervisor",0);
	if(coid==-1){
		perror("name_open");
		return EXIT_FAILURE;
	}
	printf("ecu3 started\n");
	printf("connected to supervisor\n");
	while(1){
		printf("ecu3 heartbeat\n");
		if(MsgSendPulse(coid,-1,HEARTBEAT,ECUID)==-1){
			perror("MsgSendPulse");
			break;
		}
		sleep(1);//wait before sending next heartbeat;
	}
	name_close(coid);
	return EXIT_SUCCESS;
}
