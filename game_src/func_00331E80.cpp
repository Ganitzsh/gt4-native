/* compiler: ee-gcc2.96-no-strict-aliasing */
/* Native version of 00331E80 (from its matched source): its PLZCW (the power of two at or above the image's width
   and height) in plain C. */
typedef unsigned int u32;
struct G { char pad[0x38CC4]; int flag; };
extern "C" G *D_00622F4C;
struct Image {
    int f0;
    char *data;
    int f8, fC;
    int width, height;
    int cols, rows;
    int channels;
    float aspect;
};
extern "C" {
int func_004997B0(int, u32, u32, void *);
void func_0049FD50(int, float *, float *, int, int, int);
void func_004A1638(int);
void func_004A2808(int, float);
void func_004A29A8(int);
void func_004A2C58(int, int, float);
void func_004A4550(int, int);
void func_004A4810(int, u32, u32);
float *func_004A5B70(int);
void func_004A7844(float, float, float);
void func_004A7890(float, float, float);
void func_004A79D8(float);
void func_004AA1D8(float, float, float, float);
void func_004AB040(int);
}
/* PLZCW of n-1 (its leading bits equal to the sign bit, less one) & 31, ^ 31: the bit length of n-1 (of ~(n-1)
   when negative); 1 shifted by it is the power of two at or above n. */
static inline u32 pow2(u32 n) {
    u32 x = n - 1;
    if ((int)x < 0) x = ~x;
    u32 b = 0;
    while (x) { b++; x >>= 1; }
    return 1U << b;
}
static inline float ratio(u32 n) {
    float v = (float)n;
    return v / (float)pow2(n);
}
static inline int tilebytes(Image *p) { return p->width * p->channels * p->height; }
static inline void swapf(float *a, float *b) {
    float t = *a;
    *a = *b;
    *b = t;
}

extern "C" void func_00331E80(Image *img, int rotate, float width, float height, int passes, float x, float y, float scale) {
    int flag = D_00622F4C->flag;
    if (flag) width /= 0.75f;
    float ar = width / height;
    float aspect = img->aspect;
    if (rotate) aspect = 1.0f / aspect;
    float w = width, h = height;
    if (ar < aspect) h = width / aspect;
    else w = aspect * height;
    float half = 0.5f;
    if (flag) func_004A7890(0.75f, 1.0f, 1.0f);
    func_004A7844(width * half, height * half, 0.0f);
    func_004A7890(scale, scale, scale);
    func_004A7844(x, y, 0.0f);
    if (rotate) {
        swapf(&w, &h);
        func_004A79D8(90.0f);
    }
    func_004A7844(-w * half, -h * half, 0.0f);
    u32 tw = img->width, th = img->height;
    func_004A4550(4, 2);
    func_004A4550(5, 2);
    func_004A4810(0, 0, tw - 1);
    func_004A4810(1, 0, th - 1);
    func_004A4550(3, 1);
    func_004A4550(2, 1);
    func_004AA1D8(1.0f, 1.0f, 1.0f, 1.0f);
    func_004AB040(5);
    func_004AB040(4);
    func_004A1638(6);
    func_004A29A8(0);
    float u = ratio(tw), v = ratio(th);
    int cols = img->cols, rows = img->rows;
    float dx = w / (float)cols, dy = h / (float)rows;
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            func_004997B0(1, tw, th, img->data + tilebytes(img) * (j * img->cols + i));
            float left = dx * (float)i, top = dy * (float)j;
            float bottom = top + dy;
            func_004A1638(2);
            float *p = func_004A5B70(3), *q = func_004A5B70(2);
            float right = left + dx;
            p[0] = left; p[1] = top; p[2] = 0;
            p[3] = right; p[4] = top; p[5] = 0;
            p[6] = left; p[7] = bottom; p[8] = 0;
            p[9] = right; p[10] = bottom; p[11] = 0;
            q[0] = 0; q[1] = 0; q[2] = u; q[3] = 0;
            q[4] = 0; q[5] = v; q[6] = u; q[7] = v;
            for (int k = 0; k < passes; ++k) {
                func_004A2808(100, 1.0f / ((float)k + 1.0f));
                func_004A2C58(passes, k, 0.7f);
                func_0049FD50(4, p, q, 0, 0, 0);
            }
        }
    }
}
