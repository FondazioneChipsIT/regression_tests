#ifndef DATA_H
#define DATA_H

#define DIM_IN_X  12
#define DIM_IN_Y  1
#define CH_IN     1
#define DIM_OUT_X 12
#define DIM_OUT_Y 1
#define CH_OUT    5
#define KERN_X    3
#define KERN_Y    1
#define STRIDE_X  1
#define STRIDE_Y  1
#define PAD_TOP    0
#define PAD_BOTTOM 0
#define PAD_LEFT   1
#define PAD_RIGHT  1
#define OUT_MULT  1
#define OUT_SHIFT 0
#define FLAG_RELU       0
#define FLAG_BATCH_NORM 0

DATA_LOCATION int8_t pIn[] = {
    1, // index 0
    1, // index 1
    1, // index 2
    1, // index 3
    1, // index 4
    1, // index 5
    1, // index 6
    1, // index 7
    1, // index 8
    1, // index 9
    1, // index 10
    1, // index 11
};

DATA_LOCATION int8_t pWeight[] = {
    1, // index 0
    1, // index 1
    1, // index 2
    1, // index 3
    1, // index 4
    1, // index 5
    1, // index 6
    1, // index 7
    1, // index 8
    1, // index 9
    1, // index 10
    1, // index 11
    1, // index 12
    1, // index 13
    1, // index 14
};

DATA_LOCATION int8_t pBias[] = {
    0, // index 0
    0, // index 1
    0, // index 2
    0, // index 3
    0, // index 4
};

static const int8_t ref[] = {
    2, // index 0
    2, // index 1
    2, // index 2
    2, // index 3
    2, // index 4
    3, // index 5
    3, // index 6
    3, // index 7
    3, // index 8
    3, // index 9
    3, // index 10
    3, // index 11
    3, // index 12
    3, // index 13
    3, // index 14
    3, // index 15
    3, // index 16
    3, // index 17
    3, // index 18
    3, // index 19
    3, // index 20
    3, // index 21
    3, // index 22
    3, // index 23
    3, // index 24
    3, // index 25
    3, // index 26
    3, // index 27
    3, // index 28
    3, // index 29
    3, // index 30
    3, // index 31
    3, // index 32
    3, // index 33
    3, // index 34
    3, // index 35
    3, // index 36
    3, // index 37
    3, // index 38
    3, // index 39
    3, // index 40
    3, // index 41
    3, // index 42
    3, // index 43
    3, // index 44
    3, // index 45
    3, // index 46
    3, // index 47
    3, // index 48
    3, // index 49
    3, // index 50
    3, // index 51
    3, // index 52
    3, // index 53
    3, // index 54
    2, // index 55
    2, // index 56
    2, // index 57
    2, // index 58
    2, // index 59
};

#endif // DATA_H
