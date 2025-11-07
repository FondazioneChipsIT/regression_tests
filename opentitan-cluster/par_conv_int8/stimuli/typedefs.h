#ifndef __TYPEDEFS_H__
#define __TYPEDEFS_H__

#define PI_L2 __attribute__((section(".l2_data")))
#define L2_DATA PI_L2

#define L1_GLOBAL_DATA __attribute__((section(".data_l1")))
#define L1_DATA L1_GLOBAL_DATA

#ifdef USE_CLUSTER
#define RT_LOCAL_DATA L1_DATA
#else
#define RT_LOCAL_DATA FC_DATA
#endif

#endif
