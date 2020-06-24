#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/* Pressing KEY0 should toggle the stopwatch between the run and pause states. 
Pressing KEY1 to KEY3 should set the time according to the values of the SW slider 
switches. Set the hundredths (DD) if KEY1 is pressed, the seconds (SS) for KEY2, 
and the minutes (MM) for KEY3.*/

volatile sig_atomic_t stop;
void catchSIGINT (int signum) {
	stop = 1;
}

//static File Descriptors
int sw_fd, key_fd, ledr_fd, stopwatch_fd;
int get_key(void);
int get_sw(void);
void get_stopwatch(char * stopwatch_read, int len);
void update_stopwatch(int key);

int get_key() {

	int key = 0, k = 0, read_no = 0;
	char read_in;
	char key_read[16];

	//read KEY
	for(k = 0; k < 16; k++) {
		read_no = read(key_fd, &read_in, 1);
		if (read_no > 0)
			key_read[k] = read_in;
		else {
			key_read[k] = '\0';
			k = 18;
		}
	}
	if (k == 16)
		key_read[k] = '\0';
	key = atoi(key_read);
	return key;
}

int get_sw(void) {

	int sw = 0, k = 0, read_no = 0;
	char read_in;
	char sw_read[16];
	for(k = 0; k < 8; k++) {
		read_no = read(sw_fd, &read_in, 1);
		if (read_no > 0)
			sw_read[k] = read_in;
		else {
			sw_read[k] = '\0';
			k = 9;
		}
	}

	if (k == 16) 
		sw_read[k] = '\0';
	sw = atoi(sw_read);

	return sw;
}

void get_stopwatch(char * time_read, int len) {

	int k = 0, read_no = 0;
	char read_in;
	for(k = 0; k < len; k++) {
		read_no = read(stopwatch_fd, &read_in, 1);
		if (read_no > 0)
			*(time_read + k) = read_in;
		else {
			*(time_read + k) = '\0';
			k = len+1;
		}
	}
	printf("%s\n", time_read);
	if (k == len) 
		*(time_read + k) = '\0';
}

void update_stopwatch(int key) {
	char current_time[32];
	int sw;
	get_stopwatch(current_time, 32);
	sw = get_sw();
	printf("%d\n", sw);
	switch (key) {
		case(2) : current_time[7] = (char) ((sw % 10) + 48);
				  current_time[6] = (char) (((sw / 10) % 10) + 48);
				  break;
		case(4) : current_time[4] = (char) ((sw % 10) + 48);
				  current_time[3] = (char) (((sw / 10) % 6) + 48);
				  break;
		case(8) : current_time[1] = (char) ((sw % 10) + 48);
				  current_time[0] = (char) (((sw / 10) % 6) + 48);
				  break;
		default : ;
	}
	current_time[8] = '\n';
	current_time[9] = '\0';
	printf("%s\n", current_time);
	write(stopwatch_fd, current_time, strlen(current_time));
}

int main(int argc, char * argv[]) {

	int key; //Input read
	char * stop_run[2] = {"run\n", "stop\n"};
	int stop_run_ind = 0;
	stop = 0;

	//Open File
	if ((sw_fd = open("/dev/SW", O_RDONLY)) == 1) {
		printf("Error opening /dev/SW: %s\n", strerror(errno));
		return -1;
	}

	if ((key_fd = open("/dev/KEY", O_RDONLY)) == 1) {
		printf("Error opening /dev/KEY: %s\n", strerror(errno));
		return -1;
	}

	if ((ledr_fd = open("/dev/LEDR", O_WRONLY)) == 1) {
		printf("Error opening /dev/LEDR: %s\n", strerror(errno));
		return -1;
	}

	if ((stopwatch_fd = open("/dev/stopwatch", O_RDWR)) == 1) {
		printf("Error opening /dev/stopwatch: %s\n", strerror(errno));
		return -1;
	}

	//Setup timer
	write(stopwatch_fd, stop_run[stop_run_ind % 2], (stop_run_ind % 2) + 4);
	stop_run_ind++;

	//Wait for KEY
	while (!stop) {
		key = 0;
		key = get_key();
		if (key) {
			switch (key) {

				case(1) :	printf("Stopping stopwatch\n");
							write(stopwatch_fd, stop_run[stop_run_ind % 2], (stop_run_ind % 2) + 4);
						 	stop_run_ind++;
						 	break;
				case(2) :	printf("Updating Hundredth Seconds\n");
							update_stopwatch(key);
							break;
				case(4) : 	printf("Updating Seconds\n");
							update_stopwatch(key);
							break;
				case(8) : 	printf("Updating Minutes\n");
							update_stopwatch(key);						
							break;
				default : continue;
			}
		}
	}

	//Close File
	close(sw_fd);
	close(key_fd);
	close(ledr_fd);
	close(stopwatch_fd);
	return 0;
}
