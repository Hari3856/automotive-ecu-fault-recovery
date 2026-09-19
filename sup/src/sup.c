#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <time.h>
#include <process.h>
#include <signal.h>
#include <stdint.h>

#define HEARTBEAT 1
#define FAULT_NONE 0
#define FAULT_TIMEOUT 1
#define FAULT_IPC 2
#define ECU_NOT_SEEN 0
#define ECU_HEALTHY 1
#define ECU_FAIL 2
#define ECU_COUNT 3
#define ENGINE_PRIORITY 30
#define BRAKE_PRIORITY 50
#define STEERING_PRIORITY 40
long fault_detection_delay[ECU_COUNT+1];
int braking_demand=80;//vehicle context
int steering_demand=20;
int engine_load=30;
pid_t ecu_pid[ECU_COUNT+1];//ecu process id
struct timespec fault_detect_time[ECU_COUNT+1];
struct timespec recovery_start_time[ECU_COUNT+1];
struct timespec restart_time[ECU_COUNT+1];
struct timespec verification_time[ECU_COUNT+1];

int get_dynamic_priority(int ecuid){
	int priority=0;
	if(ecuid==1) priority=ENGINE_PRIORITY+(engine_load/2);
	else if(ecuid==2) priority=BRAKE_PRIORITY+(braking_demand/2);
	else if(ecuid==3) priority=STEERING_PRIORITY+(steering_demand/2);
	return priority;
}

int find_highest_priority(int ecu_status[],int recovery_in_progress[]){
	int selected_ecu=-1;
	int highest=-1;
	for(int i=1;i<=ECU_COUNT;i++){
		if(ecu_status[i]==ECU_FAIL && recovery_in_progress[i]==0){
		int priority=get_dynamic_priority(i);
		if(priority>highest){
			highest=priority;
			selected_ecu=i;
		}
		}
	}
	return selected_ecu;
}

long elapsed_ms(struct timespec *last){ //calculates time that passed from last event to present event
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC,&now);
	return(now.tv_sec-last->tv_sec)*1000 +(now.tv_nsec-last->tv_nsec)/1000000;
}

long diff_ms(struct timespec *start,struct timespec *end){
    return (end->tv_sec-start->tv_sec)*1000+ (end->tv_nsec-start->tv_nsec)/ 1000000;
}

int recover(int ecuid){
	const char *ecupath;
	pid_t newpid;
	printf("recovering ecu%d\n",ecuid);
	//find ecupath
	if(ecuid==1) ecupath="/tmp/ecu1";
	else if(ecuid==2) ecupath="/tmp/ecu2";
	else if(ecuid==3) ecupath="/tmp/ecu3";
	else {
		printf("invalid ecuid %d\n",ecuid);
		return 0;
	}
	printf("ecu%d pid=%d\n",ecuid,ecu_pid[ecuid]);
	printf("supervisor pid=%d\n",getpid());

	if (ecu_pid[ecuid]<=0){
	    printf("ERROR: invalid pid for ecu%d:%d\n",ecuid,ecu_pid[ecuid]);
	    return 0;
	}
	if (ecu_pid[ecuid]==getpid()){
	    printf("ERROR: ecu%d pid is supervisor pid!\n",ecuid);
	    return 0;
	}
	if(kill(ecu_pid[ecuid],SIGTERM)==-1){ //SIGTERM-termination request
		perror("kill");
		return 0;
	}
	printf("ecu%d process killed\n",ecuid);
	//restart
	char *args[]={(char *)ecupath,NULL};
			newpid=spawnv(P_NOWAIT,ecupath,args);//spawnv - restarts the ecu,P_NOWAIT - it doesnt wait for finish
			if(newpid==-1) {
				perror("spawnv recovery");
				return 0;
			}
			clock_gettime(CLOCK_MONOTONIC,&restart_time[ecuid]);//restart time
			ecu_pid[ecuid]=newpid;
			printf("ecu%d restarted - newpid %d\n",ecuid,newpid);
			return 1;
}
int main(void) {
	name_attach_t *atta;
	int rid;
	struct _pulse pulse;
	struct sigevent event;
	uint64_t timeout;
	int fault;
	int ecu_status[ECU_COUNT+1]; //health table
	struct timespec last_heartbeat[ECU_COUNT+1];//store last heartbeat time
	int recovery_in_progress[ECU_COUNT+1];
	for(int i=1;i<=ECU_COUNT;i++){
		ecu_status[i]=ECU_NOT_SEEN;
		recovery_in_progress[i]=0;
	}
	int active_recovery_ecu=-1;
	atta=name_attach(NULL,"supervisor",0); //creating a named communication end point
	if(atta==NULL){
		perror("name_attach");
		return EXIT_FAILURE;
	}
	char *ecu1_args[]={"ecu1", NULL};
	char *ecu2_args[]={"ecu2", NULL};
	char *ecu3_args[]={"ecu3", NULL};
	ecu_pid[1] = spawnv(P_NOWAIT, "/tmp/ecu1", ecu1_args);
	if (ecu_pid[1]==-1)   perror("spawn ecu1");
	ecu_pid[2] = spawnv(P_NOWAIT, "/tmp/ecu2", ecu2_args);
	if (ecu_pid[2]==-1)    perror("spawn ecu2");
	ecu_pid[3] = spawnv(P_NOWAIT, "/tmp/ecu3", ecu3_args);
	if (ecu_pid[3]==-1)    perror("spawn ecu3");
	printf("ecu1 pid=%d\n",ecu_pid[1]);
	printf("ecu2 pid=%d\n",ecu_pid[2]);
	printf("ecu3 pid=%d\n",ecu_pid[3]);

	printf("Waiting for ecu heartbeats\n");
	SIGEV_UNBLOCK_INIT(&event);
	fault=FAULT_NONE;
	printf("RECOVERY SCHEDULER\n");
	printf("Engine priority %d\n",get_dynamic_priority(1));
	printf("Brake priority %d\n",get_dynamic_priority(2));
	printf("Steering priority %d\n",get_dynamic_priority(3));
	while(1){
		timeout=3LL*1000000000LL; //3 sec
		TimerTimeout(CLOCK_MONOTONIC,_NTO_TIMEOUT_RECEIVE,&event,&timeout,NULL);//timeout
		rid=MsgReceivePulse(atta->chid,&pulse,sizeof(pulse),NULL); //waiting for pulse

		if (rid==0){
			//pulse received
			if(pulse.code==HEARTBEAT){
					int ecuid=pulse.value.sival_int; //ecuid value inside pulse
					if (ecuid<1||ecuid>ECU_COUNT){
					    printf("invalid ecuid received:%d\n",ecuid);//if a random pulse is accessed,it is not considered
					    continue;
					}
					clock_gettime(CLOCK_MONOTONIC,&last_heartbeat[ecuid]);// heartbeat current time as last heartbeat time
					ecu_status[ecuid]=ECU_HEALTHY;//change status to healthy
					if(recovery_in_progress[ecuid]==1 && active_recovery_ecu==ecuid){
						clock_gettime(CLOCK_MONOTONIC,&verification_time[ecuid]);//verification time
						printf("Fault Detection: %ld ms\n", fault_detection_delay[ecuid]);
						printf("Recovery Initiation: %ld ms\n",diff_ms(&fault_detect_time[ecuid],&recovery_start_time[ecuid]));
						printf("Process Restart: %ld ms\n",diff_ms(&recovery_start_time[ecuid],&restart_time[ecuid]));
						printf("Recovery Verification: %ld ms\n",diff_ms(&restart_time[ecuid],&verification_time[ecuid]));
						printf("Total Recovery: %ld ms\n",diff_ms(&fault_detect_time[ecuid],&verification_time[ecuid]));
					    printf("ecu%d recovery verified\n",ecuid);
					    recovery_in_progress[ecuid]=0;//clears the flag when restarted ecu sends heartbeat
					    active_recovery_ecu=-1;
					}
					printf("heartbeat received from ECU %d\n",ecuid);
		}
			else{
				printf("random pulse received %d\n",pulse.code);
			}
	}
		else{
			printf("rid=%d  errno=%d\n",rid,errno);
			if(errno==ETIMEDOUT){
				fault=FAULT_TIMEOUT;
				printf("no heartbeat msg recived for 3 secs\n");
			}
			else{
				fault=FAULT_IPC;
				printf("IPC failed,errno=%d\n",errno);
				perror("MsgReceivePulse");
			}
		}
		for(int i=1;i<=ECU_COUNT;i++){
			if(ecu_status[i]==ECU_NOT_SEEN) continue; //skips the ecu that didnt send heartbeat
			long elapsed=elapsed_ms(&last_heartbeat[i]);
			if(elapsed>3000){
				if(ecu_status[i]!=ECU_FAIL){
					clock_gettime(CLOCK_MONOTONIC,&fault_detect_time[i]);//fault time
					ecu_status[i]=ECU_FAIL;
					printf("ecu%d failed\n",i);
					printf("no heartbeat for %ld ms\n",elapsed);
				}
			}
		}
		if(active_recovery_ecu==-1){
			int selected_ecu=find_highest_priority(ecu_status,recovery_in_progress);
			if(selected_ecu!=-1){
				int priority=get_dynamic_priority(selected_ecu);
				printf("recovery decision\n");
				printf("ecu%d selected\n",selected_ecu);
				printf("recovery priority=%d\n",priority);
				recovery_in_progress[selected_ecu]=1;
				active_recovery_ecu=selected_ecu;
				clock_gettime(CLOCK_MONOTONIC,&recovery_start_time[selected_ecu]);//recovery time
				if(recover(selected_ecu)== 0){
				    printf("ECU%d recovery could not be started\n",
				           selected_ecu);

				    recovery_in_progress[selected_ecu] = 0;
				    active_recovery_ecu = -1;
				}
			}
		}

	}
	name_detach(atta,0);
	return EXIT_SUCCESS;
}
