#ifndef Z800_H_
#define Z800_H_

#define Z800_IOC_MAGIC 		'Z'

/* IOCTLs for the Z800 HMD */
#define IOCTL_GET_FIRMWARE_VERSION		1
#define IOCTL_SLEEP 					0x85
#define IOCTL_CYCLEBRIGHTNESS 			0x88
#define IOCTL_KEEPALIVE 				0x8A
#define IOCTL_SET_ENABLE_3D				0x8B

/* IOCTLs for the head tracker */
#define Z800_IOCTL_HT		_IOR(Z800_IOC_MAGIC, 31, char [16])
#define Z800_IOCTL_HT_POLL	_IOW(Z800_IOC_MAGIC, 32, int)

#endif /*Z800_H_*/
