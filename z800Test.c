#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define IOCTL_GET_FIRMWARE_VERSION		1
#define IOCTL_SLEEP 					0x85
#define IOCTL_CYCLEBRIGHTNESS 			0x88
#define IOCTL_KEEPALIVE 				0x8A
#define IOCTL_SET_ENABLE_3D				0x8B

/*
 * A sample program on how to access/control the Z800.
 *
 * Author: Adam King <va3pip@gmail.com>
 * License: LGPL
 */
int main()
{
	int retValue, fd;
	unsigned char buffer[8];
	
	printf( "Test program for accessing the Z800.  Opening...\n" );
	/* open the z800 */
	fd = open( "/dev/z800:0", O_RDWR );
	if( fd == -1 ) {
		printf( "Error opening the Z800. errno = %d\n", errno );
		return -1;
	}

	/* get the firmware version */
	retValue = ioctl( fd, IOCTL_GET_FIRMWARE_VERSION, buffer, 8 );
	if( retValue == -1 ) {
		printf( "Error getting firmware version. errno = %d\n", errno );
		return -1;
	}
	printf( "Z800 Firmware Version: %x.%x\n", buffer[0], buffer[1] );

	// wake it up
	retValue = ioctl( fd, IOCTL_KEEPALIVE );
	if( retValue == -1 ) {
		printf( "Error trying to wake the z800. errno = %d\n", errno );
		return -1;
	}
	printf( "Woke up the Z800\n" );
			
	/* Release the Z800 */
	retValue = close(fd);
	if( retValue == -1 ) {
		printf( "Error closing the z800. errno = %d\n", errno );
		return -1;
	}	
	
	printf( "Exiting...\n" );
	return 0;
}
