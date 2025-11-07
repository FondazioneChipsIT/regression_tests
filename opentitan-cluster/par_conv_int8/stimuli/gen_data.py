#!/usr/bin/env python3
import argparse
import numpy as np
from math import floor

# Quantization helper
def quant_i8(value, mult, shift, relu=False):
    res = (value * mult) >> shift
    if relu:
        res = max(0, res)
    return np.int8(np.clip(res, -128, 127))

def conv2d_int8(input, weight, bias, stride, padding, out_mult, out_shift, relu):
    dim_in_y, dim_in_x, ch_in = input.shape
    ch_out, _, dim_k_y, dim_k_x = weight.shape
    pad_top, pad_bottom, pad_left, pad_right = padding

    dim_out_y = floor((dim_in_y + pad_top + pad_bottom - dim_k_y) / stride[1]) + 1
    dim_out_x = floor((dim_in_x + pad_left + pad_right - dim_k_x) / stride[0]) + 1

    out = np.zeros((dim_out_y, dim_out_x, ch_out), dtype=np.int8)
    inp_padded = np.zeros((dim_in_y + pad_top + pad_bottom,
                            dim_in_x + pad_left + pad_right,
                            ch_in), dtype=np.int8)
    inp_padded[pad_top:pad_top+dim_in_y, pad_left:pad_left+dim_in_x, :] = input

    for y in range(dim_out_y):
        for x in range(dim_out_x):
            for oc in range(ch_out):
                acc = 0
                for ky in range(dim_k_y):
                    for kx in range(dim_k_x):
                        for ic in range(ch_in):
                            acc += int(inp_padded[y*stride[1] + ky, x*stride[0] + kx, ic]) * \
                                   int(weight[oc, ic, ky, kx])
                acc += int(bias[oc])
                out[y, x, oc] = quant_i8(acc, out_mult, out_shift, relu)
    return out

def format_array(name, arr, qualifier):
    """
    Format an array so that each element is on its own line,
    with a trailing comment indicating its index.
    qualifier should include storage specifiers and type, e.g.
      "DATA_LOCATION int8_t" or "static const int8_t"
    """
    lines = []
    for idx, v in enumerate(arr):
        lines.append(f"    {int(v)}, // index {idx}")
    body = "\n".join(lines)
    return f"{qualifier} {name}[] = {{\n{body}\n}};\n\n"

def main():
    p = argparse.ArgumentParser(
        description="Generate data.h for convolution i8 test with unitary inputs & weights"
    )
    # input dimensions
    p.add_argument('--dim_in_x', type=int, required=True)
    p.add_argument('--dim_in_y', type=int, required=True)
    p.add_argument('--ch_in', type=int, required=True)
    # output channels / filters
    p.add_argument('--ch_out', type=int, required=True)
    # kernel
    p.add_argument('--kern_x', type=int, required=True)
    p.add_argument('--kern_y', type=int, required=True)
    # stride
    p.add_argument('--stride_x', type=int, default=1)
    p.add_argument('--stride_y', type=int, default=1)
    # padding
    p.add_argument('--pad_top', type=int, default=0)
    p.add_argument('--pad_bottom', type=int, default=0)
    p.add_argument('--pad_left', type=int, default=0)
    p.add_argument('--pad_right', type=int, default=0)
    # quant params
    p.add_argument('--out_mult', type=int, default=1)
    p.add_argument('--out_shift', type=int, default=0)
    # flags
    p.add_argument('--relu', action='store_true')
    p.add_argument('--batch_norm', action='store_true')
    p.add_argument('-o', '--output', default='stimuli/data.h')
    args = p.parse_args()

    # compute output dims
    dim_out_y = floor((args.dim_in_y + args.pad_top + args.pad_bottom - args.kern_y) / args.stride_y) + 1
    dim_out_x = floor((args.dim_in_x + args.pad_left + args.pad_right - args.kern_x) / args.stride_x) + 1

    # UNITARY tensors
    in_shape = (args.dim_in_y, args.dim_in_x, args.ch_in)
    input  = np.ones(in_shape, dtype=np.int8)
    weight = np.ones((args.ch_out, args.ch_in, args.kern_y, args.kern_x), dtype=np.int8)
    bias   = np.zeros((args.ch_out,), dtype=np.int8)

    # compute reference output
    ref = conv2d_int8(
        input, weight, bias,
        stride=(args.stride_x, args.stride_y),
        padding=(args.pad_top, args.pad_bottom, args.pad_left, args.pad_right),
        out_mult=args.out_mult,
        out_shift=args.out_shift,
        relu=args.relu
    )

    # flatten arrays
    inp_flat = input.ravel()
    w_flat   = weight.ravel(order='C')
    b_flat   = bias.ravel()
    r_flat   = ref.ravel()

    # emit C header
    with open(args.output, 'w') as f:
        f.write('#ifndef DATA_H\n#define DATA_H\n\n')
        # input specs
        f.write(f'#define DIM_IN_X  {args.dim_in_x}\n')
        f.write(f'#define DIM_IN_Y  {args.dim_in_y}\n')
        f.write(f'#define CH_IN     {args.ch_in}\n')
        # output specs
        f.write(f'#define DIM_OUT_X {dim_out_x}\n')
        f.write(f'#define DIM_OUT_Y {dim_out_y}\n')
        f.write(f'#define CH_OUT    {args.ch_out}\n')
        # kernel specs
        f.write(f'#define KERN_X    {args.kern_x}\n')
        f.write(f'#define KERN_Y    {args.kern_y}\n')
        # stride
        f.write(f'#define STRIDE_X  {args.stride_x}\n')
        f.write(f'#define STRIDE_Y  {args.stride_y}\n')
        # padding
        f.write(f'#define PAD_TOP    {args.pad_top}\n')
        f.write(f'#define PAD_BOTTOM {args.pad_bottom}\n')
        f.write(f'#define PAD_LEFT   {args.pad_left}\n')
        f.write(f'#define PAD_RIGHT  {args.pad_right}\n')
        # quant
        f.write(f'#define OUT_MULT  {args.out_mult}\n')
        f.write(f'#define OUT_SHIFT {args.out_shift}\n')
        # flags
        f.write(f'#define FLAG_RELU       {int(args.relu)}\n')
        f.write(f'#define FLAG_BATCH_NORM {int(args.batch_norm)}\n\n')

        # arrays with index comments
        f.write(format_array('pIn',     inp_flat, 'DATA_LOCATION int8_t'))
        f.write(format_array('pWeight', w_flat,   'DATA_LOCATION int8_t'))
        f.write(format_array('pBias',   b_flat,   'DATA_LOCATION int8_t'))
        f.write(format_array('ref',      r_flat,   'static const int8_t'))
        f.write('#endif // DATA_H\n')

    print(f"Wrote {args.output} with unitary inputs, weights & zero bias")

if __name__ == '__main__':
    main()
