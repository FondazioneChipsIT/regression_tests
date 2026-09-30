#include "pulp.h"
#include <stdio.h>

#define JOB_DESC_ADDR ARCHI_L2_SHARED_ADDR

#define JOB_CMD_IDLE 0x00000000u
#define JOB_CMD_RUN  0xAA000001u
#define JOB_CMD_EXIT 0xAA0000FFu

typedef struct {
    volatile unsigned int cmd;
    volatile unsigned int seq;
    volatile unsigned int arg0;
    volatile unsigned int arg1;
    volatile unsigned int retval;
} cluster_job_desc_t;

int main(void)
{
    volatile cluster_job_desc_t *job = (volatile cluster_job_desc_t *)JOB_DESC_ADDR;

    if (get_core_id() == 0)
    {
        job->arg0 += 1;
        //printf("[cluster] seq=%u arg0=%u\r\n", job->seq, job->arg0);
    }

    synch_barrier();

    return (int)job->seq;
}
