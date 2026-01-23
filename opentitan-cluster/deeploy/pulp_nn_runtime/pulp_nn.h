// This file contains few functions from pulp-nn-mixed re-adapted to run on pulp-runtime

#include "pulp.h"

#define min(a,b)                                                ((a)<(b)?(a):(b))
#define clip8(x)                                                __builtin_pulp_clipu_r(x, 255)
#define SumDotp4(a, b, c)                                       __builtin_pulp_sdotusp4(a, b, c)
#define log2(x)                                                 __builtin_pulp_fl1(x)
#define max4(a,b)                                               __builtin_pulp_maxu4(a,b)

#define NUM_CORES 8

////////////////////////////////////////////////
//           PULP NN UTILS FUNCTIONS          //
////////////////////////////////////////////////

// PULP ZERO MEM

static void __attribute__((noinline)) pulp_zero_mem(uint8_t * pBuffer, unsigned int size)
{
  int lfover = size &0x3;
  for (int i=0; i<(size>>2); i++)
  {
    *((v4u *)pBuffer) = (v4u){0,0,0,0};
    // MemoryFence();
    pBuffer+=4;
  }
  while(lfover)
  {
    *pBuffer++=0;
    lfover--;
  }
}

// PULP NN IM2COL U8 TO U8

static void __attribute__((noinline)) pulp_nn_im2col_u8_to_u8(uint8_t * pInput, uint8_t * pOutput, unsigned int blockSize)
{
  unsigned int blkCnt = blockSize >> 2u;
  int lfover = blockSize & 0x03;
  for (int i = 0; i<blkCnt; i++)
  {
    *((v4u*)pOutput) = *((v4u*)pInput);
    pInput+=4;
    pOutput+=4;
  }
  while (lfover)
  {
    *((uint8_t*)pOutput) = *((uint8_t*)pInput);
    pOutput++;
    pInput++;
    lfover--;
  }
}

// PULP NN QUANT U8

static uint8_t __attribute__((noinline)) pulp_nn_quant_u8 (
  int32_t phi,
  int16_t m,
  int8_t d
  ) {
  int32_t x = (m * phi) >> d;
  uint8_t res = clip8(x);
  return res;
}

// PULP NN BN QUANT U8

static uint8_t __attribute__((noinline)) pulp_nn_bn_quant_u8 (
  int32_t phi,
  int64_t k,
  int64_t lambda,
  int8_t d
  ) {
  int64_t integer_image_phi = (k * phi) + lambda;
  int64_t x = (integer_image_phi) >> d;
  uint8_t res = clip8(x);
  return res;
}

static void __attribute__((noinline)) pulp_nn_compare_and_replace_if_larger_u8(uint8_t * base,
                                                uint8_t * target,
                                                uint16_t length)
{
  uint8_t *pIn = base;
  uint8_t *pCom = target;
  v4u inp;
  v4u com;
  int cnt = length >> 2;

  while(cnt > 0u)
  {
    inp = *((v4u*)pIn);
    com = *((v4u*)pCom);

    *((v4u*)pIn) = max4(inp, com);

    pCom+=4;
    pIn+=4;
    cnt--;
  }

  int left = length & 0x3;
  while (left>0u)
  {
    if(*pIn<*pCom)
      *pIn=*pCom;
    
    pIn++;
    pCom++;
    left--;
  }
}

////////////////////////////////////////////////
//         PULP NN KERNELS FUNCTIONS          //
////////////////////////////////////////////////

// PULP NN MATMUL U8 U8 I8

uint8_t *pulp_nn_matmul_u8_u8_i8(
                        uint8_t *pIn,
                        int8_t *pBias,
                        uint8_t *pOut,
                        uint8_t *pOut2,
                        int8_t *pWeight,
                        int32_t *pKappa,
                        int32_t *pLambda,
                        uint16_t out_mult,
                        uint16_t out_shift,
                        uint16_t num_col_im2col,
                        uint16_t ch_out,
                        uint8_t flag_relu,
                        uint8_t flag_batch_norm)
{
  v4s vecA;
  v4s vecA2;
  v4s vecA3;
  v4s vecA4;
  v4u vecB;
  v4u vecB2;

  uint16_t ch_out_r = ch_out;
  uint16_t num_col_im2col_w = num_col_im2col;

  //uint8_t *pOut2 = pOut + ch_out_r;
  int8_t *pA = pWeight;

  uint16_t chan_left = ch_out & 0x3;

  for(int i=0; i < (ch_out >> 2); i++)
  {
    uint8_t *pB =  pIn;
    uint8_t *pB2 = (pB + num_col_im2col);
    int8_t *pA2 = (pA + num_col_im2col_w);
    int8_t *pA3 = (pA2 + num_col_im2col_w);
    int8_t *pA4 = (pA3 + num_col_im2col_w);

    int sum = 0;
    int sum2 = 0;
    int sum3 = 0;
    int sum4 = 0;
    int sum5 = 0;
    int sum6 = 0;
    int sum7 = 0;
    int sum8 = 0;

    if (pBias != NULL)
    {
      sum = *((int*)  pBias);
      pBias+= 4;
      sum2 = *((int*)  pBias);
      pBias+= 4;
      sum3 = *((int*)  pBias);
      pBias+= 4;
      sum4 = *((int*)  pBias);
      pBias+= 4;

      sum5 = sum;
      sum6 = sum2;
      sum7 = sum3;
      sum8 = sum4;
    }

    for(int j=0; j<(num_col_im2col_w >> 2); j++)
    {
      vecA = *((v4s*)pA);
      vecA2 = *((v4s*)pA2);
      vecA3 = *((v4s*)pA3);
      vecA4 = *((v4s*)pA4);

      vecB = *((v4u*)pB);
      vecB2 = *((v4u*)pB2);

      sum = SumDotp4(vecB, vecA, sum );
      sum2 = SumDotp4(vecB, vecA2, sum2);
      sum3 = SumDotp4(vecB, vecA3, sum3);
      sum4 = SumDotp4(vecB, vecA4, sum4);

      sum5 = SumDotp4(vecB2, vecA, sum5);
      sum6 = SumDotp4(vecB2, vecA2, sum6);
      sum7 = SumDotp4(vecB2, vecA3, sum7);
      sum8 = SumDotp4(vecB2, vecA4, sum8);

      pA+=4;
      pA2+=4;
      pA3+=4;
      pA4+=4;

      pB+=4;
      pB2+=4;
    }
    uint16_t col_cnt_im2col = num_col_im2col & 0x3;
    while (col_cnt_im2col > 0)
    {
      int8_t inA = *pA++;
      int8_t inA2 = *pA2++;
      int8_t inA3 = *pA3++;
      int8_t inA4 = *pA4++;
      uint8_t inB = *pB++;
      uint8_t inB2 = *pB2++;
      asm volatile("": : :"memory");
      sum += inA * inB;
      sum2 += inA2 * inB;
      sum3 += inA3 * inB;
      sum4 += inA4 * inB;
      sum5 += inA * inB2;
      sum6 += inA2 * inB2;
      sum7 += inA3 * inB2;
      sum8 += inA4 * inB2;

      col_cnt_im2col--;
    }
    if (flag_batch_norm && flag_relu)
    {
      *pOut = pulp_nn_bn_quant_u8(sum, *pKappa, *pLambda, out_shift);
      pOut++;
      *pOut2 = pulp_nn_bn_quant_u8(sum5, *pKappa, *pLambda, out_shift);
      pOut2++;
      pKappa++;
      pLambda++;

      *pOut = pulp_nn_bn_quant_u8(sum2, *pKappa, *pLambda, out_shift);
      pOut++;
      *pOut2 = pulp_nn_bn_quant_u8(sum6, *pKappa, *pLambda, out_shift);
      pOut2++;
      pKappa++;
      pLambda++;

      *pOut = pulp_nn_bn_quant_u8(sum3, *pKappa, *pLambda, out_shift);
      pOut++;
      *pOut2 = pulp_nn_bn_quant_u8(sum7, *pKappa, *pLambda, out_shift);
      pOut2++;
      pKappa++;
      pLambda++;

      *pOut = pulp_nn_bn_quant_u8(sum4, *pKappa, *pLambda, out_shift);
      pOut++;
      *pOut2 = pulp_nn_bn_quant_u8(sum8, *pKappa, *pLambda, out_shift);
      pOut2++;
      pKappa++;
      pLambda++;
    }
    else
    {
      if (flag_relu == 1)
      {
        *pOut = pulp_nn_quant_u8(sum, out_mult, out_shift);
        pOut++;
        *pOut = pulp_nn_quant_u8(sum2, out_mult, out_shift);
        pOut++;
        *pOut = pulp_nn_quant_u8(sum3, out_mult, out_shift);
        pOut++;
        *pOut = pulp_nn_quant_u8(sum4, out_mult, out_shift);
        pOut++;

        *pOut2 = pulp_nn_quant_u8(sum5, out_mult, out_shift);
        pOut2++;
        *pOut2 = pulp_nn_quant_u8(sum6, out_mult, out_shift);
        pOut2++;
        *pOut2 = pulp_nn_quant_u8(sum7, out_mult, out_shift);
        pOut2++;
        *pOut2 = pulp_nn_quant_u8(sum8, out_mult, out_shift);
        pOut2++;
      }
      else
      {
        *pOut = (uint8_t) clip8(sum >> out_shift);
        pOut++;
        *pOut = (uint8_t) clip8(sum2 >> out_shift);
        pOut++;
        *pOut = (uint8_t) clip8(sum3 >> out_shift);
        pOut++;
        *pOut = (uint8_t) clip8(sum4 >> out_shift);
        pOut++;

        *pOut2 = (uint8_t) clip8(sum5 >> out_shift);
        pOut2++;
        *pOut2 = (uint8_t) clip8(sum6 >> out_shift);
        pOut2++;
        *pOut2 = (uint8_t) clip8(sum7 >> out_shift);
        pOut2++;
        *pOut2 = (uint8_t) clip8(sum8 >> out_shift);
        pOut2++;
      }
    }
    pA+=(3 * num_col_im2col_w);
  }
   while(chan_left)
  {
    uint8_t *pB = pIn;
    uint8_t *pB2 = (pB + num_col_im2col);
    int sum = 0;
    if (pBias != NULL){
      sum = *((int*) pBias);
      pBias += 4;
    }
    int sum2 = sum;

    for(int j=0; j < (num_col_im2col_w >> 2); j++)
    {
      vecA = *((v4s*) pA);
      vecB = *((v4u*) pB);
      vecB2 = *((v4u*) pB2);

      sum = SumDotp4(vecB, vecA, sum);
      sum2 = SumDotp4(vecB2, vecA, sum2);

      pA+=4;
      pB+=4;
      pB2+=4;
    }
    uint16_t col_cnt_im2col = num_col_im2col & 0x3;
    while(col_cnt_im2col > 0)
    {
      int8_t inA = *pA++;
      uint8_t inB = *pB++;
      uint8_t inB2 = *pB2++;
      asm volatile("": : :"memory");
      sum += inA * inB;
      sum2 += inA * inB2;

      col_cnt_im2col--;
    }
    if (flag_batch_norm && flag_relu)
    {
      *pOut = pulp_nn_bn_quant_u8(sum, *pKappa, *pLambda, out_shift);
      pOut++;
      *pOut2 = pulp_nn_bn_quant_u8(sum2, *pKappa, *pLambda, out_shift);
      pOut2++;
      pKappa++;
      pLambda++;
    }
    else
    {
      if (flag_relu == 1)
      {
        *pOut = pulp_nn_quant_u8(sum, out_mult, out_shift);
        pOut++;
        *pOut2 = pulp_nn_quant_u8(sum2, out_mult, out_shift);
        pOut2++;
      }
      else
      {
        *pOut = (uint8_t) clip8(sum >> out_shift);
        pOut++;
        *pOut2 = (uint8_t) clip8(sum2 >> out_shift);
        pOut2++;
      }
    }
    chan_left--;
  }
  pOut+=ch_out_r;
  return pOut;
}


void pulp_nn_conv_u8_u8_i8(
                        uint8_t *pIn,
                        uint8_t *pIm2ColBuffer,
                        int8_t *pBias,
                        uint8_t *pOut,
                        int8_t *pWeight,
                        int32_t *pKappa,
                        int32_t *pLambda,
                        uint16_t out_mult,
                        uint16_t out_shift,
                        uint16_t dim_in_x,
                        uint16_t dim_in_y,
                        uint16_t ch_in,
                        uint16_t dim_out_x,
                        uint16_t dim_out_y,
                        uint16_t ch_out,
                        uint16_t dim_kernel_x,
                        uint16_t dim_kernel_y,
                        uint16_t padding_y_top,
                        uint16_t padding_y_bottom,
                        uint16_t padding_x_left,
                        uint16_t padding_x_right,
                        uint16_t stride_x,
                        uint16_t stride_y,
                        uint8_t flag_relu,
                        uint8_t flag_batch_norm)
{
  uint16_t ch_in_r = ch_in;
  uint16_t ch_out_r = ch_out;

  int core_id = rt_core_id();
  uint8_t * pIm2ColBase = pIm2ColBuffer + (2 * core_id * ch_in * dim_kernel_x * dim_kernel_y);
  int i_out_y, i_out_x, i_ker_y, i_ker_x;
  int Log2Core;

  uint8_t extra_chunk = ((dim_out_y & (NUM_CORES-1)) != 0);
  uint8_t extra_chunk_r;
  uint16_t dim_out_x_r;
  uint8_t section;
  int core_id_r;

  if(extra_chunk && dim_out_x > 1)
  {
    Log2Core = log2(NUM_CORES >> 1);
    core_id_r = (core_id >> 1);
    dim_out_x_r = (dim_out_x >> 1);
    section = (core_id & 0x1);
    extra_chunk_r = ((dim_out_y & ((NUM_CORES >> 1) - 1)) != 0);
  }
  else
  {
    Log2Core = log2(NUM_CORES);
    core_id_r = core_id;
    dim_out_x_r = dim_out_x;
    section = 0;
    extra_chunk_r = extra_chunk;
    extra_chunk = 0;
  }

  uint8_t flag_dim_out_x_odd = dim_out_x & 0x01;

  int chunk = (dim_out_y >> Log2Core) + extra_chunk_r;

  int start_pixel = min((chunk * core_id_r), dim_out_y);
  int stop_pixel = min(start_pixel + chunk, dim_out_y);

  uint8_t *pIm2Col = pIm2ColBase;
  uint8_t *pOutBuffer = pOut + (start_pixel * ch_out_r * dim_out_x) + (section * ch_out_r * dim_out_x_r);

  for (i_out_y = start_pixel; i_out_y < stop_pixel; i_out_y++)
  {
    for(i_out_x=(section * dim_out_x_r); i_out_x<(dim_out_x_r + (section * (dim_out_x_r + flag_dim_out_x_odd))); i_out_x++)
    {
      if(i_out_y < padding_y_top)
      {
        for(i_ker_y=((i_out_y * stride_y) - padding_y_top); i_ker_y<((i_out_y * stride_y) - padding_y_top + dim_kernel_y); i_ker_y++)
        {
          for(i_ker_x=((i_out_x * stride_x) - padding_x_left); i_ker_x<((i_out_x * stride_x) - padding_x_left + dim_kernel_x); i_ker_x++)
          {
            if((i_ker_y < 0) || (i_ker_y >= dim_in_y) || (i_ker_x < 0) || (i_ker_x >= dim_in_x))
            {
              pulp_zero_mem(pIm2Col, ch_in);
            }
            else
            {
              pulp_nn_im2col_u8_to_u8((uint8_t*) (pIn + ((i_ker_y * dim_in_x + i_ker_x) * ch_in_r)), pIm2Col, ch_in);
            }
            pIm2Col+=ch_in;
          }
        }
      }
      else if(i_out_y < dim_out_y - padding_y_bottom)
      {
        if(i_out_x < padding_x_left)
        {
          for(i_ker_y=((i_out_y * stride_y) - padding_y_top); i_ker_y<((i_out_y * stride_y) - padding_y_top + dim_kernel_y); i_ker_y++)
          {
            for(i_ker_x=((i_out_x * stride_x) - padding_x_left); i_ker_x<((i_out_x * stride_x) - padding_x_left + dim_kernel_x); i_ker_x++)
            {
              if((i_ker_x < 0) || (i_ker_x >= dim_in_x))
              {
                pulp_zero_mem(pIm2Col, ch_in);
              }
              else
              {
                pulp_nn_im2col_u8_to_u8((uint8_t*) (pIn + ((i_ker_y * dim_in_x + i_ker_x) * ch_in_r)), pIm2Col, ch_in);
              }
              pIm2Col+=ch_in;
            }
          }
        }
        else if(i_out_x < (dim_out_x - padding_x_right))
        {
          for(i_ker_y=((i_out_y * stride_y) - padding_y_top); i_ker_y<((i_out_y * stride_y) - padding_y_top + dim_kernel_y); i_ker_y++)
          {
            pulp_nn_im2col_u8_to_u8((uint8_t*) pIn + (i_ker_y * dim_in_x + i_out_x * stride_x - padding_x_left)*ch_in_r,pIm2Col,ch_in * dim_kernel_x);
            pIm2Col+=(ch_in * dim_kernel_x);
          }
        }
        else
        {
          for(i_ker_y=((i_out_y * stride_y) - padding_y_top); i_ker_y<((i_out_y * stride_y) - padding_y_top + dim_kernel_y); i_ker_y++)
          {
            for(i_ker_x = i_out_x * stride_x - padding_x_left; i_ker_x < i_out_x * stride_x - padding_x_left + dim_kernel_x; i_ker_x++)
            {
              if((i_ker_x < 0) || (i_ker_x >= dim_in_x))
              {
                pulp_zero_mem(pIm2Col, ch_in);
              }
              else
              {
                pulp_nn_im2col_u8_to_u8((uint8_t *)pIn + (i_ker_y*dim_in_x+i_ker_x)* ch_in_r, pIm2Col, ch_in);
              }
              pIm2Col+=ch_in;
            }
          }
        }
      }
      else
      {
        for(i_ker_y=((i_out_y * stride_y) - padding_y_top); i_ker_y<((i_out_y * stride_y) - padding_y_top + dim_kernel_y); i_ker_y++)
        {
          for(i_ker_x = i_out_x * stride_x - padding_x_left; i_ker_x < i_out_x * stride_x - padding_x_left + dim_kernel_x; i_ker_x++)
          {
            if(i_ker_y < 0 || (i_ker_y >= dim_in_y) || i_ker_x < 0 || i_ker_x >= dim_in_x)
            {
              pulp_zero_mem(pIm2Col, ch_in);
            }
            else
            {
              pulp_nn_im2col_u8_to_u8((uint8_t *) pIn + (i_ker_y * dim_in_x + i_ker_x) * ch_in_r, pIm2Col, ch_in);
            }
            pIm2Col+=ch_in;
          }
        }
      }
      if(pIm2Col == (pIm2ColBase + ((ch_in * dim_kernel_x * dim_kernel_y) << 1)))
      {
        pOutBuffer = pulp_nn_matmul_u8_u8_i8(
          pIm2ColBase,
          pBias,
          pOutBuffer,
          pOutBuffer + ch_out_r,
          pWeight,
          pKappa,
          pLambda,
          out_mult,
          out_shift,
          (ch_in * dim_kernel_x * dim_kernel_y),
          ch_out,
          flag_relu,
          flag_batch_norm
          );

        pIm2Col = pIm2ColBase;
      }
    }
  //   pOut+=(extra_chunk * ((dim_out_x_r + ((1 - section) * flag_dim_out_x_odd)) * ch_out_r));
  // }

    if(pIm2Col != pIm2ColBase)
    {
      const int8_t *pA = pWeight;
      int i;
      int32_t * k1 = pKappa;
      int32_t * lambda1 = pLambda;
      v4s inA[1];
      v4u inB;
      uint8_t out[1];
      for(i = 0; i < ch_out; i++)
      {
        int sum = 0;
        if (pBias != NULL)
        {
          sum = *((int*) pBias);
          pBias += 4;
        }

        uint8_t *pB = pIm2ColBase;
        uint16_t col_cnt_im2col = ch_in * dim_kernel_x * dim_kernel_y >> 2;
        for(int j=0; j < col_cnt_im2col; j++)
        {
          v4s inA = *((v4s*) pA);
          v4u inB = *((v4u*) pB);

          sum = SumDotp4(inB, inA, sum);
          pA+=4;
          pB+=4;
        }
        col_cnt_im2col = (ch_in * dim_kernel_y * dim_kernel_x) & 0x3;
        while (col_cnt_im2col)
        {
          int8_t inA1 = *pA++;
          uint8_t inB1 = *pB++;
          asm volatile("": : :"memory");
          sum += inA1 * inB1;

          col_cnt_im2col--;
        }
        if (flag_batch_norm && flag_relu)
        {
          *pOutBuffer = pulp_nn_bn_quant_u8(sum, *k1, *lambda1, out_shift);
          k1++;
          lambda1++;
          pOutBuffer++;
        }
        else
        {
          if(flag_relu == 1)
          {
            *pOutBuffer = pulp_nn_quant_u8(sum, out_mult, out_shift);
            pOutBuffer++;
          }
          else
          {
            *pOutBuffer = (uint8_t) clip8(sum >> out_shift);
            pOutBuffer++;
          }
        }
      }
    }
    pOutBuffer+=(extra_chunk * ((dim_out_x_r + ((1 - section) * flag_dim_out_x_odd)) * ch_out_r));
    pIm2Col = pIm2ColBase;
  }
  synch_barrier();
}

void __attribute__ ((noinline)) pulp_nn_maxpool_u8(
                                                  uint8_t * pIn,
                                                  uint8_t * pOut,
  uint16_t  dim_im_in_x,
  uint16_t  dim_im_in_y,
  uint16_t  ch_im_in,
  uint16_t  dim_im_out_x,
  uint16_t  dim_im_out_y,
  uint16_t  dim_kernel_x,
  uint16_t  dim_kernel_y,
  uint16_t  padding_t,
  uint16_t  padding_b,
  uint16_t  padding_l,
  uint16_t  padding_r,
  uint16_t  stride_x,
  uint16_t  stride_y)
{
  int core_id = rt_core_id();
  int n_cores = NUM_CORES;
  if (dim_im_in_y < NUM_CORES)
  {
    n_cores = dim_im_in_y;
  }
  int  Log2Core = log2(n_cores);
  int ch_im_in_r = ch_im_in;

  int chunck = (dim_im_in_y >> Log2Core) + ((dim_im_in_y & (NUM_CORES-1))!=0);

  int start = min(chunck * core_id, dim_im_in_y);
  int stop = min(start + chunck, dim_im_in_y);
  int   i_x, i_y;

  for (i_y = start; i_y < stop; i_y++)
  {
    for (i_x = 0; i_x < dim_im_out_x; i_x++)
    {
      /* for each output pixel */
      uint8_t     *target = pIn + (i_y * dim_im_in_x + i_x) * ch_im_in_r;
      uint8_t     *win_start;
      uint8_t     *win_stop;
      if (i_x * stride_x - padding_l < 0)
      {
        win_start = target;
      }
      else
      {
        win_start = pIn + (i_y * dim_im_in_x + i_x * stride_x - padding_l) * ch_im_in_r;
      }

      if (i_x * stride_x - padding_l + dim_kernel_x >= dim_im_in_x)
      {
        win_stop = pIn + (i_y * dim_im_in_x + dim_im_in_x) * ch_im_in_r;
      }
      else
      {
        win_stop = pIn + (i_y * dim_im_in_x + i_x * stride_x - padding_l + dim_kernel_x) * ch_im_in_r;
      }

      /* first step is to copy over initial data */
      for (int i = 0; i< ch_im_in_r; i++) target[i] = win_start[i];

      /* start the max operation from the second part */
      win_start += ch_im_in_r;
      for (; win_start < win_stop; win_start += ch_im_in_r)
      {
        pulp_nn_compare_and_replace_if_larger_u8(target, win_start, ch_im_in_r);
      }
    }
  }

  synch_barrier();
  if (dim_im_out_y < NUM_CORES)
  {
    n_cores = dim_im_out_y;
  }
  Log2Core = log2(n_cores);
  int chunck2 = (dim_im_out_y >> Log2Core) + ((dim_im_out_y & (NUM_CORES-1))!=0);
  int start2 = chunck2 * core_id;
  int stop2 = min(start2 + chunck2, dim_im_out_y);

    /* then does the pooling along y axis */
  for (i_y = start2; i_y < stop2; i_y++)
  {
    /* for each output row */
    uint8_t *target = pOut + i_y * dim_im_out_x * ch_im_in_r;
    uint8_t *row_start;
    uint8_t *row_end;
    /* setting the starting row */
    if (i_y * stride_y - padding_t < 0)
    {
      row_start = pIn;
    }
    else
    {
      row_start = pIn + (i_y * stride_y - padding_t) * dim_im_in_x * ch_im_in_r;
    }
    /* setting the stopping row */
    if (i_y * stride_y - padding_t + dim_kernel_y >= dim_im_in_y)
    {
      row_end = pIn + dim_im_in_y * dim_im_in_x * ch_im_in_r;
    }
    else
    {
      row_end = pIn + (i_y * stride_y - padding_t + dim_kernel_y) * dim_im_in_x * ch_im_in_r;
    }

    /* copy over the first row */
    for (int i = 0; i< dim_im_out_x * ch_im_in_r; i++)
    {
      target[i] = (uint8_t) row_start[i];
    }
    /* move over to next row */
    row_start += ch_im_in_r * dim_im_in_x;

    for (; row_start < row_end; row_start += dim_im_in_x * ch_im_in_r)
    {
      pulp_nn_compare_and_replace_if_larger_u8(target, row_start, dim_im_out_x * ch_im_in_r);
    }
  }
  synch_barrier();
}

void pulp_nn_linear_u8_u8_i8(
                        uint8_t *pIn,
                        int8_t *pBias,
                        uint8_t *pOut,
                        int8_t *pWeight,
                        int32_t *pKappa,
                        int32_t *pLambda,
                        uint16_t out_mult,
                        uint16_t out_shift,
                        uint16_t dim_vec,
                        uint16_t num_o_neurons,
                        uint8_t flag_relu,
                        uint8_t flag_batch_norm)
{
    uint16_t dim_vec_in = dim_vec;
    uint16_t dim_vec_wt = dim_vec;

    int core_id = rt_core_id();
    int Log2Core = log2(NUM_CORES);
    int chunk = (num_o_neurons >> Log2Core) + ((num_o_neurons & (NUM_CORES-1))!=0);
    int start = min(chunk * core_id, num_o_neurons);
    int stop = min(start + chunk, num_o_neurons);

    v4u vecA;
    v4s vecB;
    v4s vecB2;

    uint8_t *pOutBuffer = (uint8_t *) pOut + start;
    int lft_neurons = chunk & 0x01;
    int stop_even = stop - lft_neurons;

    int i;
    int32_t *k1 = pKappa + start;
    int32_t *lambda1 = pLambda + start;

    for(i=start; i<stop_even; i+=2)
    {
        int sum = 0;
        int sum2 = 0;
        if (pBias != NULL)
        {
          sum = *(int32_t *)(pBias + 4*i);
          sum2 = *(int32_t *)(pBias + 4*i + 4);
        }


        uint8_t *pA = pIn;
        int8_t *pB = pWeight + (i * dim_vec_wt);
        int8_t *pB2 = pB + dim_vec_wt;

        for (int j=0; j<(dim_vec >> 2); j++)
        {
          vecA = *((v4u*)pA);
          vecB = *((v4s*)pB);
          vecB2 = *((v4s*)pB2);
          sum = SumDotp4(vecA, vecB, sum);
          sum2 = SumDotp4(vecA, vecB2, sum2);
          pA+=4;
          pB+=4;
          pB2+=4;
        }
        uint16_t col_cnt = dim_vec & 0x3;
        while (col_cnt)
        {
          uint8_t inA = *pA;
          pA++;
          int8_t inB = *pB;
          pB++;
          int8_t inB2 = *pB2;
          pB2++;
          sum += inA * inB;
          sum2 += inA * inB2;
          col_cnt--;
        }
        if (flag_batch_norm && flag_relu)
        {
          *pOutBuffer = pulp_nn_bn_quant_u8(sum, *k1, *lambda1, out_shift);
          pOutBuffer++;
          *pOutBuffer = pulp_nn_bn_quant_u8(sum2, *(k1 + 1), *(lambda1 + 1), out_shift);
          pOutBuffer++;
          k1+=2;
          lambda1+=2;
        }
        else
        {
          if (flag_relu == 1)
          {
            *pOutBuffer = pulp_nn_quant_u8(sum, out_mult, out_shift);
            pOutBuffer++;
            *pOutBuffer = pulp_nn_quant_u8(sum2, out_mult, out_shift);
            pOutBuffer++;
          }
          else
          {
            *pOutBuffer = (uint8_t) clip8(sum >> out_shift);
            pOutBuffer++;
            *pOutBuffer = (uint8_t) clip8(sum2 >> out_shift);
            pOutBuffer++;
          }
        }
    }
    if (lft_neurons && (stop - start) > 0)
    {
        int sum = 0;
        if (pBias != NULL)
        {
          sum = *(int32_t *)(pBias + 4*i);
        }

        uint8_t *pA = pIn;
        int8_t *pB = pWeight + (i * dim_vec_wt);

        for (int j=0; j<(dim_vec >> 2); j++)
        {
            vecA = *((v4u*)pA);
            vecB = *((v4s*)pB);
            sum = SumDotp4(vecA, vecB, sum);
            pA+=4;
            pB+=4;
        }
        uint16_t col_cnt = dim_vec & 0x3;
        while (col_cnt)
        {
          uint8_t inA = *pA;
          pA++;
          int8_t inB = *pB;
          pB++;
          sum += inA * inB;
          col_cnt--;
        }
        if (flag_batch_norm && flag_relu)
        {
          *pOutBuffer = pulp_nn_bn_quant_u8(sum, *pKappa, *pLambda, out_shift);
          pOutBuffer++;
          pKappa++;
          pLambda++;
        }
        else
        {
          if (flag_relu == 1)
          {
            *pOutBuffer = pulp_nn_quant_u8(sum, out_mult, out_shift);
            pOutBuffer++;
          }
          else
          {
            *pOutBuffer = (uint8_t) clip8(sum >> out_shift);
            pOutBuffer++;
          }
        }
    }
    synch_barrier();
}
