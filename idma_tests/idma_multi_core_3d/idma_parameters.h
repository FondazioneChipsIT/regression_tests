typedef struct {
  unsigned int size;
  unsigned int length;
  unsigned int src_stride_2d;
  unsigned int dst_stride_2d;
  unsigned int src_stride_3d;
  unsigned int dst_stride_3d;
  unsigned int num_reps_3d;
} TransferParameters;

TransferParameters transfer_params[] = {
  {16, 3, 3, 3, 5, 5, 2},
  {16, 2, 4, 4, 4, 4, 3},
};

